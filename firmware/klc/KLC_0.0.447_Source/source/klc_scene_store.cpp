#include "klc_scene_store.h"

#include "klc_diag.h"
#include "klc_ota.h"
#include "klc_storage.h"
#include "klc_storage_coordinator.h"
#include "klc_scenes.h"
#include "klc_chain.h"
#include <LittleFS.h>
#include <string.h>

namespace {

constexpr uint8_t KLC_SCENE_STORE_MAGIC[4] = {'K','L','S','1'};
constexpr uint8_t KLC_SCENE_STORE_MARKER_MAGIC[4] = {'K','L','M','1'};
constexpr uint8_t KLC_SCENE_STORE_AUTH_MAGIC[4] = {'K','L','A','2'};
constexpr uint8_t KLC_SCENE_STORE_TXN_MAGIC[4] = {'K','L','T','1'};
constexpr size_t KLC_SCENE_STORE_HEADER_LEN = 24U;
constexpr size_t KLC_SCENE_STORE_PAYLOAD_MAX = 512U;
constexpr size_t KLC_SCENE_STORE_CHUNK = 64U;
constexpr char KLC_SCENE_STORE_MARKER_PATH[] = "/klc_scenes_v1.ok";
constexpr char KLC_SCENE_STORE_MARKER_TMP_PATH[] = "/klc_scenes_v1.tmp";
constexpr char KLC_SCENE_STORE_AUTH_PATHS[2][24] = {
  "/klc_scenes_auth_a.kla", "/klc_scenes_auth_b.kla"};
constexpr char KLC_SCENE_STORE_TXN_PATHS[2][24] = {
  "/klc_scenes_txn_a.klt", "/klc_scenes_txn_b.klt"};
constexpr size_t KLC_SCENE_STORE_RECORD_LEN = 40U;
constexpr size_t KLC_SCENE_STORE_RECORD_V2_LEN = 32U;
constexpr size_t KLC_SCENE_STORE_RECORD_LEGACY_LEN = 24U;
constexpr uint8_t KLC_SCENE_STATUS_HISTORY_SIZE = 32U;
constexpr uint8_t KLC_SCENE_TXN_PREPARED = 1U;
constexpr uint8_t KLC_SCENE_TXN_MIGRATING = 2U;
constexpr uint8_t KLC_SCENE_TXN_COMPLETE = 3U;
constexpr uint8_t KLC_SCENE_TXN_CANCELLED = 4U;
constexpr uint8_t KLC_SCENE_TXN_FAILED = 5U;
constexpr uint8_t KLC_SCENE_TXN_AWAIT_ACTIVATION = 6U;
constexpr char KLC_SCENE_BOOT_GENERATION_PATH[] = "/klc_scene_boot.gen";

struct KlcScenePayloadWriter {
  uint8_t* out;
  size_t capacity;
  size_t used;
  bool ok;
  void u8(uint8_t value) { if (used >= capacity) { ok=false; return; } out[used++]=value; }
  void u16(uint16_t value) { u8((uint8_t)value);u8((uint8_t)(value>>8)); }
  void u32(uint32_t value) { u16((uint16_t)value);u16((uint16_t)(value>>16)); }
  void bytes(const void* source,size_t length) { if (!ok||length>capacity-used){ok=false;return;}memcpy(out+used,source,length);used+=length; }
};

struct KlcScenePayloadReader {
  const uint8_t* in;
  size_t length;
  size_t used;
  bool ok;
  uint8_t u8(){if(used>=length){ok=false;return 0;}return in[used++];}
  uint16_t u16(){const uint16_t a=u8(),b=u8();return (uint16_t)(a|(b<<8));}
  uint32_t u32(){const uint32_t a=u16(),b=u16();return a|(b<<16);}
  void bytes(void* target,size_t count){if(!ok||count>length-used){ok=false;return;}memcpy(target,in+used,count);used+=count;}
};

struct KlcSceneSlotInfo {
  bool valid;
  uint8_t slot;
  uint32_t sequence;
  uint16_t payload_length;
  uint32_t payload_crc;
  uint32_t edit_revision;
};

struct KlcSceneStoreJob {
  bool used;
  bool needs_persist;
  KlcSceneStoreState state;
  uint8_t scene_id;
  uint8_t slot;
  uint64_t operation_id;
  uint32_t sequence;
  uint32_t edit_revision;
  uint32_t previous_revision;
  bool runtime_applied;
  bool rollback_failed;
  KlcSceneStoreFailurePhase failure_phase;
  KlcSceneConfig scene;
  KlcSceneConfig previous_scene;
  uint8_t payload[KLC_SCENE_STORE_PAYLOAD_MAX];
  uint16_t payload_length;
  uint16_t offset;
  uint32_t verify_crc;
  uint32_t started_ms;
  uint32_t bytes_written;
  uint32_t bytes_read;
  uint32_t max_step_us;
  uint32_t free_heap_start;
};

KlcSceneStoreJob g_active{};
KlcSceneStoreJob g_queue[KLC_SCENE_STORE_QUEUE_CAPACITY]{};
File g_scene_file;
uint8_t g_header[KLC_SCENE_STORE_HEADER_LEN]{};
uint8_t g_verify_chunk[KLC_SCENE_STORE_CHUNK]{};
uint32_t g_applied_revision[KLC_SCENE_MAX_PUBLIC + 1U]{};
uint32_t g_accepted_revision[KLC_SCENE_MAX_PUBLIC + 1U]{};
uint32_t g_persisted_revision[KLC_SCENE_MAX_PUBLIC + 1U]{};
uint32_t g_next_sequence[KLC_SCENE_MAX_PUBLIC + 1U]{};
uint32_t g_persisted_crc[KLC_SCENE_MAX_PUBLIC + 1U]{};
uint16_t g_persisted_length[KLC_SCENE_MAX_PUBLIC + 1U]{};
bool g_scene_dirty[KLC_SCENE_MAX_PUBLIC + 1U]{};
uint8_t g_persisted_slot[KLC_SCENE_MAX_PUBLIC + 1U]{};
uint32_t g_next_operation_counter = 1U;
uint32_t g_boot_generation = 0U;
uint32_t g_recovery_count = 0U;
bool g_initialized = false;
KlcSceneStoreStatus g_last_status{};
KlcSceneStoreStatus g_status_history[KLC_SCENE_STATUS_HISTORY_SIZE]{};
KlcSceneStoreStatus g_scene_status[KLC_SCENE_MAX_PUBLIC + 1U]{};
KlcScenePersistenceStatus g_persistence_status[KLC_SCENE_MAX_PUBLIC + 1U]{};
uint8_t g_status_history_next = 0U;
bool g_scene_writer_held = false;
KlcStorageWriterToken g_scene_writer_token = 0U;
uint32_t g_current_step_started_us = 0U;
bool g_boot_generation_ready = false;

struct KlcSceneControlRecord {
  bool valid;
  uint8_t slot;
  uint32_t sequence;
  uint32_t value;
  uint8_t state;
  uint8_t progress;
  uint8_t failed_scene;
  uint8_t flags;
  uint64_t operation_id;
  uint32_t error_code;
  uint32_t config_generation;
};

void klcSceneStoreCopyText(char* target,size_t target_len,const char* text)
{
  if(target==nullptr||target_len==0U)return;
  snprintf(target,target_len,"%s",text!=nullptr?text:"");
}

void klcSceneStorePath(uint8_t scene_id,uint8_t slot,char* out,size_t out_len)
{
  snprintf(out,out_len,"/scn%02u%c.kls",(unsigned)scene_id,slot==0U?'a':'b');
}

void klcSceneStorePut16(uint8_t* out,uint16_t value)
{ out[0]=(uint8_t)value;out[1]=(uint8_t)(value>>8); }
void klcSceneStorePut32(uint8_t* out,uint32_t value)
{ klcSceneStorePut16(out,(uint16_t)value);klcSceneStorePut16(out+2,(uint16_t)(value>>16)); }
uint16_t klcSceneStoreGet16(const uint8_t* in)
{ return (uint16_t)(in[0]|((uint16_t)in[1]<<8)); }
uint32_t klcSceneStoreGet32(const uint8_t* in)
{ return (uint32_t)klcSceneStoreGet16(in)|((uint32_t)klcSceneStoreGet16(in+2)<<16); }

bool klcSceneStoreEnsureBootGeneration()
{
  if(g_boot_generation_ready)return true;
  uint32_t previous=0U;File input=LittleFS.open(KLC_SCENE_BOOT_GENERATION_PATH,"r");
  if(input){uint8_t data[12];const bool ok=input.size()==sizeof(data)&&
    input.read(data,sizeof(data))==(int)sizeof(data);input.close();
    if(ok&&memcmp(data,"KBG1",4U)==0&&
       klcSceneStoreGet32(data+8)==klcSceneStoreCrc32(data,8U))
      previous=klcSceneStoreGet32(data+4);
  }
  g_boot_generation=previous+1U;if(g_boot_generation==0U)g_boot_generation=1U;
  uint8_t data[12]={'K','B','G','1'};klcSceneStorePut32(data+4,g_boot_generation);
  klcSceneStorePut32(data+8,klcSceneStoreCrc32(data,8U));
  File output=LittleFS.open(KLC_SCENE_BOOT_GENERATION_PATH,"w");
  if(!output)return false;const bool wrote=output.write(data,sizeof(data))==sizeof(data);
  output.flush();output.close();if(!wrote)return false;
  uint8_t verify[12];input=LittleFS.open(KLC_SCENE_BOOT_GENERATION_PATH,"r");
  const bool verified=input&&input.read(verify,sizeof(verify))==(int)sizeof(verify)&&
    memcmp(data,verify,sizeof(data))==0;if(input)input.close();
  g_boot_generation_ready=verified;return verified;
}

uint64_t klcSceneStoreNextOperationId()
{
  uint32_t counter=g_next_operation_counter++;
  if(counter==0U)counter=g_next_operation_counter++;
  if(g_next_operation_counter==0U)g_next_operation_counter=1U;
  return ((uint64_t)g_boot_generation<<32)|counter;
}

bool klcSceneStoreReadControlRecord(const char* path,const uint8_t magic[4],
                                    uint8_t slot,KlcSceneControlRecord& record)
{
  record={};record.slot=slot;
  File file=LittleFS.open(path,"r");if(!file)return false;
  const size_t length=file.size();
  uint8_t data[KLC_SCENE_STORE_RECORD_LEN]{};
  const bool supported=length==KLC_SCENE_STORE_RECORD_LEN||
                       length==KLC_SCENE_STORE_RECORD_V2_LEN||
                       length==KLC_SCENE_STORE_RECORD_LEGACY_LEN;
  const bool ok=supported&&file.read(data,length)==(int)length;file.close();
  const size_t crc_offset=length==KLC_SCENE_STORE_RECORD_LEN?36U:
    (length==KLC_SCENE_STORE_RECORD_V2_LEN?28U:20U);
  if(!ok||memcmp(data,magic,4U)!=0||
     klcSceneStoreGet16(data+4)!=KLC_SCENE_STORE_SCHEMA||
     data[6]!=KLC_SCENE_MAX_PUBLIC||data[7]!=slot||
     klcSceneStoreGet32(data+crc_offset)!=klcSceneStoreCrc32(data,crc_offset))return false;
  record.valid=true;record.sequence=klcSceneStoreGet32(data+8);
  record.value=klcSceneStoreGet32(data+12);record.state=data[16];
  if(length>=KLC_SCENE_STORE_RECORD_V2_LEN){
    record.progress=data[17];record.failed_scene=data[18];record.flags=data[19];
    record.operation_id=klcSceneStoreGet32(data+20);
    record.error_code=klcSceneStoreGet32(data+24);
  }
  if(length==KLC_SCENE_STORE_RECORD_LEN){
    record.operation_id|=(uint64_t)klcSceneStoreGet32(data+28)<<32;
    record.config_generation=klcSceneStoreGet32(data+32);
  }
  return true;
}

bool klcSceneStoreNewestControlRecord(const char paths[2][24],
                                      const uint8_t magic[4],
                                      KlcSceneControlRecord& newest)
{
  KlcSceneControlRecord a{},b{};
  const bool va=klcSceneStoreReadControlRecord(paths[0],magic,0U,a);
  const bool vb=klcSceneStoreReadControlRecord(paths[1],magic,1U,b);
  if(!va&&!vb){newest={};return false;}
  newest=vb&&(!va||klcSceneStoreSequenceNewer(b.sequence,a.sequence))?b:a;
  return true;
}

bool klcSceneStoreWriteControlRecord(const char paths[2][24],
                                     const uint8_t magic[4],uint32_t value,
                                     uint8_t state,uint64_t operation_id=0U,
                                     uint8_t progress=0U,uint8_t failed_scene=0U,
                                     uint32_t error_code=0U,
                                     uint32_t config_generation=0U,
                                     uint8_t flags=0U)
{
  KlcSceneControlRecord current{};
  const bool has_current=klcSceneStoreNewestControlRecord(paths,magic,current);
  const uint8_t slot=has_current?(uint8_t)(1U-current.slot):0U;
  const uint32_t sequence=has_current?current.sequence+1U:1U;
  uint8_t data[KLC_SCENE_STORE_RECORD_LEN]{};memcpy(data,magic,4U);
  klcSceneStorePut16(data+4,KLC_SCENE_STORE_SCHEMA);data[6]=KLC_SCENE_MAX_PUBLIC;
  data[7]=slot;klcSceneStorePut32(data+8,sequence);
  klcSceneStorePut32(data+12,value);data[16]=state;data[17]=progress;
  data[18]=failed_scene;data[19]=flags;klcSceneStorePut32(data+20,(uint32_t)operation_id);
  klcSceneStorePut32(data+24,error_code);
  klcSceneStorePut32(data+28,(uint32_t)(operation_id>>32));
  klcSceneStorePut32(data+32,config_generation);
  klcSceneStorePut32(data+36,klcSceneStoreCrc32(data,36U));
  File file=LittleFS.open(paths[slot],"w");if(!file)return false;
  const bool wrote=file.write(data,sizeof(data))==sizeof(data);file.flush();file.close();
  if(!wrote)return false;
  KlcSceneControlRecord verified{};
  return klcSceneStoreReadControlRecord(paths[slot],magic,slot,verified)&&
    verified.sequence==sequence&&verified.value==value&&verified.state==state&&
    verified.operation_id==operation_id&&verified.progress==progress&&
    verified.failed_scene==failed_scene&&verified.error_code==error_code&&
    verified.config_generation==config_generation&&verified.flags==flags;
}

bool klcSceneStorePayloadFingerprint(const KlcSceneConfig& scene,
                                     uint32_t& crc,uint16_t& length)
{
  uint8_t payload[KLC_SCENE_STORE_PAYLOAD_MAX];
  if(!klcSceneStoreEncodePayload(scene,payload,sizeof(payload),length))return false;
  crc=klcSceneStoreCrc32(payload,length);return true;
}

bool klcSceneStoreConfigDigest(const KlcDeviceConfig& cfg,uint32_t& digest)
{
  // Die Generation umfasst alle fuer Pool, PIO/DMA, LED-Ausgabe und Leistung
  // relevanten Hauptconfigwerte plus die Szenenzielmenge. Netzwerkreset und
  // reine Zugangsdaten bleiben bewusst ausserhalb: sie duerfen die KLS-
  // Generation nicht veraendern. Feldweise Kodierung vermeidet Struct-Padding.
  if(cfg.output_count>KLC_MAX_OUTPUTS||
     cfg.power.group_count>KLC_MAX_POWER_GROUPS)return false;
  digest=0U;uint8_t data[KLC_SCENE_STORE_PAYLOAD_MAX];
  for(uint8_t index=0U;index<cfg.output_count;++index){
    const KlcOutputConfig& o=cfg.outputs[index];KlcScenePayloadWriter w{data,sizeof(data),0U,true};
    const size_t name_len=strnlen(o.name,KLC_MAX_NAME_LEN);
    w.u8(index);w.u8(o.id);w.u8(o.enabled);w.u8((uint8_t)name_len);w.bytes(o.name,name_len);
    w.u8(o.follows_output);w.u8(o.chain_reverse);w.u8(o.gpio);w.u16(o.pixels);
    w.u16(o.string_segment_count);w.u16((uint16_t)o.pixel_offset);
    w.u8(o.power_on_state_on);w.u8(o.power_on_mode);w.u8(o.power_on_r);
    w.u8(o.power_on_g);w.u8(o.power_on_b);w.u8(o.power_on_w);w.u8(o.power_on_scene);
    w.u8(o.led_send_mode);w.u16(o.led_send_interval_seconds);w.u8(o.type);
    w.u8(o.chipset);w.u8(o.color_order);w.u8(o.power_group);w.u32(o.limit_ma);
    w.u8(o.max_brightness);w.u8(o.knx_on_mode);w.u8(o.knx_on_r);w.u8(o.knx_on_g);
    w.u8(o.knx_on_b);w.u8(o.knx_on_w);w.u8(o.knx_on_scene);
    w.u16(o.knx_on_ramp_ms);w.u16(o.knx_off_ramp_ms);
    if(!w.ok)return false;digest=klcSceneStoreCrc32(data,w.used,digest);
  }
  {KlcScenePayloadWriter w{data,sizeof(data),0U,true};
    w.u8(cfg.output_count);w.u8(cfg.global_brightness);
    w.u32(cfg.power.controller_limit_ma);w.u32(cfg.power.idle_ma);
    w.u8(cfg.power.safety_percent);w.u8(cfg.power.r_ma_per_pixel);
    w.u8(cfg.power.g_ma_per_pixel);w.u8(cfg.power.b_ma_per_pixel);
    w.u8(cfg.power.w_ma_per_pixel);w.u8(cfg.power.standby_ma_per_pixel);
    w.u8(cfg.power.group_count);
    for(uint8_t index=0U;index<cfg.power.group_count;++index){
      const KlcPowerGroupConfig& group=cfg.power.groups[index];
      const size_t name_len=strnlen(group.name,KLC_MAX_NAME_LEN);
      w.u8(group.id);w.u8((uint8_t)name_len);w.bytes(group.name,name_len);
      w.u32(group.limit_ma);
    }
    if(!w.ok)return false;digest=klcSceneStoreCrc32(data,w.used,digest);
  }
  for(uint8_t id=1U;id<=KLC_SCENE_MAX_PUBLIC;++id){
    uint16_t length=0U;
    if(!klcSceneStoreEncodePayload(cfg.scenes[id],data,sizeof(data),length))return false;
    const uint8_t prefix[3]={id,(uint8_t)length,(uint8_t)(length>>8)};
    digest=klcSceneStoreCrc32(prefix,sizeof(prefix),digest);
    digest=klcSceneStoreCrc32(data,length,digest);
  }
  return true;
}

void klcSceneStoreBuildHeader(uint8_t scene_id,uint8_t slot,uint32_t sequence,
                              uint16_t payload_length,uint32_t payload_crc,
                              uint32_t edit_revision,
                              uint8_t* header)
{
  memset(header,0,KLC_SCENE_STORE_HEADER_LEN);
  memcpy(header,KLC_SCENE_STORE_MAGIC,4U);
  klcSceneStorePut16(header+4,KLC_SCENE_STORE_SCHEMA);
  header[6]=scene_id;header[7]=slot;
  klcSceneStorePut32(header+8,sequence);
  klcSceneStorePut16(header+12,payload_length);
  // Die bisher reservierten zwei Bytes tragen ab 0.0.446 die persistierte
  // Bearbeitungsrevision. Wert 0 alter KLS1-Dateien wird beim Lesen zu 1.
  klcSceneStorePut16(header+14,(uint16_t)edit_revision);
  klcSceneStorePut32(header+16,payload_crc);
  klcSceneStorePut32(header+20,klcSceneStoreCrc32(header,20U));
}

bool klcSceneStoreHeaderValid(const uint8_t* header,uint8_t scene_id,
                              uint8_t slot,KlcSceneSlotInfo& info)
{
  info={};info.slot=slot;
  if(memcmp(header,KLC_SCENE_STORE_MAGIC,4U)!=0||
     klcSceneStoreGet16(header+4)!=KLC_SCENE_STORE_SCHEMA||
     header[6]!=scene_id||header[7]!=slot||
     klcSceneStoreGet32(header+20)!=klcSceneStoreCrc32(header,20U)) return false;
  const uint16_t length=klcSceneStoreGet16(header+12);
  if(length==0U||length>KLC_SCENE_STORE_PAYLOAD_MAX)return false;
  info.valid=true;info.sequence=klcSceneStoreGet32(header+8);
  info.payload_length=length;info.payload_crc=klcSceneStoreGet32(header+16);
  info.edit_revision=klcSceneStoreGet16(header+14);
  if(info.edit_revision==0U)info.edit_revision=1U;
  return true;
}

bool klcSceneStoreReadSlot(uint8_t scene_id,uint8_t slot,
                           KlcSceneSlotInfo& info,KlcSceneConfig* scene)
{
  char path[24];klcSceneStorePath(scene_id,slot,path,sizeof(path));
  File file=LittleFS.open(path,"r");if(!file){info={};return false;}
  uint8_t header[KLC_SCENE_STORE_HEADER_LEN];
  if(file.read(header,sizeof(header))!=(int)sizeof(header)||
     !klcSceneStoreHeaderValid(header,scene_id,slot,info)||
     file.size()!=(size_t)KLC_SCENE_STORE_HEADER_LEN+info.payload_length){file.close();info.valid=false;return false;}
  uint8_t payload[KLC_SCENE_STORE_PAYLOAD_MAX];
  if(file.read(payload,info.payload_length)!=(int)info.payload_length){file.close();info.valid=false;return false;}
  file.close();
  if(klcSceneStoreCrc32(payload,info.payload_length)!=info.payload_crc){info.valid=false;return false;}
  if(scene!=nullptr){
    if(!klcSceneStoreDecodePayload(scene_id,payload,info.payload_length,*scene)){
      info.valid=false;return false;
    }
    char validation[160];
    if(!klcConfigValidateSceneDetailed(*scene,scene_id,validation,sizeof(validation))){
      info.valid=false;
      char note[208];snprintf(note,sizeof(note),
        "Szene %u Slot %c semantisch ungueltig: %.150s",scene_id,
        slot==0U?'A':'B',validation);
      Serial.print("[SCENE-STORE][VALIDATION] ");Serial.println(note);
      klcDiagLogWarning(KLC_DIAG_EVENT_SCENE_RECOVERED,note);
      return false;
    }
  }
  return true;
}

void klcSceneStoreSlotFailureReason(uint8_t scene_id,uint8_t slot,
                                    char* reason,size_t reason_len)
{
  char path[24];klcSceneStorePath(scene_id,slot,path,sizeof(path));
  File file=LittleFS.open(path,"r");
  if(!file){klcSceneStoreCopyText(reason,reason_len,"Slot fehlt");return;}
  uint8_t header[KLC_SCENE_STORE_HEADER_LEN];KlcSceneSlotInfo info{};
  if(file.read(header,sizeof(header))!=(int)sizeof(header)||
     !klcSceneStoreHeaderValid(header,scene_id,slot,info)){
    file.close();klcSceneStoreCopyText(reason,reason_len,"Headerfehler");return;
  }
  if(file.size()!=KLC_SCENE_STORE_HEADER_LEN+info.payload_length){
    file.close();klcSceneStoreCopyText(reason,reason_len,"Laengenfehler");return;
  }
  uint8_t payload[KLC_SCENE_STORE_PAYLOAD_MAX];
  if(file.read(payload,info.payload_length)!=(int)info.payload_length){
    file.close();klcSceneStoreCopyText(reason,reason_len,"Lesefehler");return;
  }
  file.close();
  if(klcSceneStoreCrc32(payload,info.payload_length)!=info.payload_crc){
    klcSceneStoreCopyText(reason,reason_len,"CRC-Fehler");return;
  }
  KlcSceneConfig scene{};char validation[96];
  if(!klcSceneStoreDecodePayload(scene_id,payload,info.payload_length,scene)||
     !klcConfigValidateSceneDetailed(scene,scene_id,validation,sizeof(validation))){
    klcSceneStoreCopyText(reason,reason_len,"semantisch ungueltig");return;
  }
  klcSceneStoreCopyText(reason,reason_len,"unbekannter Slotfehler");
}

bool klcSceneStoreWriteSlotSync(uint8_t scene_id,uint8_t slot,
                                uint32_t sequence,const KlcSceneConfig& scene)
{
  uint8_t payload[KLC_SCENE_STORE_PAYLOAD_MAX];uint16_t length=0U;
  if(!klcSceneStoreEncodePayload(scene,payload,sizeof(payload),length))return false;
  uint8_t header[KLC_SCENE_STORE_HEADER_LEN];
  klcSceneStoreBuildHeader(scene_id,slot,sequence,length,
                           klcSceneStoreCrc32(payload,length),1U,header);
  char path[24];klcSceneStorePath(scene_id,slot,path,sizeof(path));
  File file=LittleFS.open(path,"w");if(!file)return false;
  const bool wrote=file.write(header,sizeof(header))==sizeof(header)&&
                   file.write(payload,length)==length;
  file.flush();file.close();if(!wrote)return false;
  KlcSceneSlotInfo verified;KlcSceneConfig decoded;
  if(!klcSceneStoreReadSlot(scene_id,slot,verified,&decoded)||
     verified.sequence!=sequence)return false;
  uint8_t decoded_payload[KLC_SCENE_STORE_PAYLOAD_MAX];uint16_t decoded_length=0U;
  return klcSceneStoreEncodePayload(decoded,decoded_payload,sizeof(decoded_payload),decoded_length)&&
         decoded_length==length&&memcmp(decoded_payload,payload,length)==0;
}

uint8_t klcSceneStoreWinningSlot(bool va,const KlcSceneSlotInfo& a,
                                 bool vb,const KlcSceneSlotInfo& b)
{
  // Bei gleicher Generation gewinnt wie beim normalen Boot deterministisch A.
  return vb&&(!va||klcSceneStoreSequenceNewer(b.sequence,a.sequence))?1U:0U;
}

bool klcSceneStoreTargetWins(bool va,const KlcSceneSlotInfo& a,
                             bool vb,const KlcSceneSlotInfo& b,
                             uint32_t target_crc,uint16_t target_len)
{
  if(!va&&!vb)return false;
  // Gleiche Sequenz mit abweichendem Inhalt ist niemals eine eindeutige
  // Generation, auch dann nicht, wenn der deterministische A-Tiebreak passt.
  if(va&&vb&&a.sequence==b.sequence&&
     (a.payload_crc!=b.payload_crc||a.payload_length!=b.payload_length))return false;
  const KlcSceneSlotInfo& winner=
    klcSceneStoreWinningSlot(va,a,vb,b)==1U?b:a;
  return winner.valid&&winner.payload_crc==target_crc&&
         winner.payload_length==target_len;
}

bool klcSceneStoreMakeTargetNewest(uint8_t scene_id,
                                   const KlcSceneConfig& target,
                                   KlcSceneSlotInfo& a,KlcSceneConfig& sa,bool& va,
                                   KlcSceneSlotInfo& b,KlcSceneConfig& sb,bool& vb)
{
  uint32_t crc=0U;uint16_t length=0U;
  if(!klcSceneStorePayloadFingerprint(target,crc,length))return false;
  if(klcSceneStoreTargetWins(va,a,vb,b,crc,length))return true;
  const uint8_t winner=klcSceneStoreWinningSlot(va,a,vb,b);
  const uint8_t target_slot=!va&&!vb?0U:(uint8_t)(1U-winner);
  // Arithmetischer uint32_t-Ueberlauf ist Bestandteil der seriellen
  // Wrap-around-Ordnung: 0 folgt auf UINT32_MAX und ist damit neuer.
  const uint32_t next=!va&&!vb?1U:(winner==1U?b.sequence:a.sequence)+1U;
  if(!klcSceneStoreWriteSlotSync(scene_id,target_slot,next,target))return false;
  va=klcSceneStoreReadSlot(scene_id,0U,a,&sa);
  vb=klcSceneStoreReadSlot(scene_id,1U,b,&sb);
  return klcSceneStoreTargetWins(va,a,vb,b,crc,length);
}

bool klcSceneStoreMarkerValid()
{
  KlcSceneControlRecord authority{};
  if(klcSceneStoreNewestControlRecord(KLC_SCENE_STORE_AUTH_PATHS,
                                      KLC_SCENE_STORE_AUTH_MAGIC,authority))return true;
  File file=LittleFS.open(KLC_SCENE_STORE_MARKER_PATH,"r");if(!file)return false;
  uint8_t marker[12];const bool read=file.read(marker,sizeof(marker))==(int)sizeof(marker);file.close();
  return read&&memcmp(marker,KLC_SCENE_STORE_MARKER_MAGIC,4U)==0&&
    klcSceneStoreGet16(marker+4)==KLC_SCENE_STORE_SCHEMA&&
    marker[6]==KLC_SCENE_MAX_PUBLIC&&marker[7]==0U&&
    klcSceneStoreGet32(marker+8)==klcSceneStoreCrc32(marker,8U);
}

bool klcSceneStoreWriteMarker(uint32_t config_generation)
{
  // A/B-Kontrollsatz: der bisher gueltige Autoritaetssatz wird niemals vor
  // erfolgreichem Schreiben und Zuruecklesen des inaktiven Satzes entfernt.
  return klcSceneStoreWriteControlRecord(KLC_SCENE_STORE_AUTH_PATHS,
    KLC_SCENE_STORE_AUTH_MAGIC,config_generation,1U,0U,0U,0U,0U,
    config_generation,3U);
}

uint8_t klcSceneStoreQueueDepth()
{
  uint8_t count=g_active.used?1U:0U;
  for(uint8_t i=0;i<KLC_SCENE_STORE_QUEUE_CAPACITY;++i)if(g_queue[i].used)++count;
  return count;
}

void klcSceneStorePublish(const KlcSceneStoreJob& job,KlcSceneStoreState state,
                          const char* error)
{
  g_last_status.initialized=g_initialized;g_last_status.busy=
    state!=KLC_SCENE_STORE_DONE&&state!=KLC_SCENE_STORE_FAILED&&
    state!=KLC_SCENE_STORE_SUPERSEDED;
  g_last_status.ram_only=g_scene_dirty[job.scene_id];
  g_last_status.scene_id=job.scene_id;g_last_status.slot=job.slot;
  g_last_status.queue_depth=klcSceneStoreQueueDepth();g_last_status.state=state;
  g_last_status.operation_id=job.operation_id;
  g_last_status.operation_revision=job.edit_revision;
  g_last_status.accepted_revision=g_accepted_revision[job.scene_id];
  g_last_status.applied_revision=g_applied_revision[job.scene_id];
  g_last_status.current_scene_revision=g_applied_revision[job.scene_id];
  g_last_status.ram_revision=g_applied_revision[job.scene_id];
  g_last_status.persisted_revision=g_persisted_revision[job.scene_id];
  g_last_status.storage_sequence=job.sequence;
  g_last_status.bytes_written=job.bytes_written;g_last_status.bytes_read=job.bytes_read;
  g_last_status.started_ms=job.started_ms;
  g_last_status.completed_ms=(state==KLC_SCENE_STORE_DONE||state==KLC_SCENE_STORE_FAILED)?millis():0U;
  g_last_status.max_step_us=job.max_step_us;g_last_status.recovery_count=g_recovery_count;
  g_last_status.runtime_applied=job.runtime_applied;
  g_last_status.rollback_failed=job.rollback_failed;
  g_last_status.failure_phase=job.failure_phase;
  g_last_status.free_heap_start=job.free_heap_start;
  g_last_status.free_heap_end=(uint32_t)rp2040.getFreeHeap();
  if(state==KLC_SCENE_STORE_DONE||state==KLC_SCENE_STORE_FAILED){
    g_last_status.littlefs_total_bytes=klcOtaGetLittleFsTotalBytes();
    g_last_status.littlefs_used_bytes=klcOtaGetLittleFsUsedBytes();
  }
  klcSceneStoreCopyText(g_last_status.error,sizeof(g_last_status.error),error);
  if(job.scene_id<=KLC_SCENE_MAX_PUBLIC){
    g_scene_status[job.scene_id]=g_last_status;
    KlcScenePersistenceStatus& ps=g_persistence_status[job.scene_id];
    ps.scene_id=job.scene_id;ps.ram_revision=g_applied_revision[job.scene_id];
    ps.accepted_revision=g_accepted_revision[job.scene_id];
    ps.applied_revision=g_applied_revision[job.scene_id];
    ps.persisted_revision=g_persisted_revision[job.scene_id];
    ps.dirty=g_scene_dirty[job.scene_id];ps.ram_only=ps.dirty;
    ps.active_operation_id=g_last_status.busy?job.operation_id:0U;
    ps.queued=state==KLC_SCENE_STORE_QUEUED;
    if(state==KLC_SCENE_STORE_DONE){ps.last_success_operation_id=job.operation_id;
      ps.last_success_timestamp=millis();ps.last_error_code=0U;ps.last_error_text[0]='\0';}
    else if(state==KLC_SCENE_STORE_FAILED){ps.last_error_code=1U;
      klcSceneStoreCopyText(ps.last_error_text,sizeof(ps.last_error_text),error);}
  }
}

void klcSceneStoreArchiveLastStatus()
{
  g_status_history[g_status_history_next]=g_last_status;
  g_status_history_next=(uint8_t)((g_status_history_next+1U)%
                                  KLC_SCENE_STATUS_HISTORY_SIZE);
}

void klcSceneStoreRefreshSlotStatus(uint8_t scene_id)
{
  KlcSceneSlotInfo a{},b{};KlcSceneConfig sa{},sb{};
  const bool va=klcSceneStoreReadSlot(scene_id,0U,a,&sa);
  const bool vb=klcSceneStoreReadSlot(scene_id,1U,b,&sb);
  KlcScenePersistenceStatus& ps=g_persistence_status[scene_id];
  ps.slot_a_valid=va;ps.slot_b_valid=vb;
  if(!va&&!vb)return;
  const bool use_b=klcSceneStoreWinningSlot(va,a,vb,b)==1U;
  const KlcSceneSlotInfo& chosen=use_b?b:a;
  g_persisted_slot[scene_id]=chosen.slot;
  g_next_sequence[scene_id]=chosen.sequence;
  g_persisted_revision[scene_id]=chosen.edit_revision;
  g_persisted_crc[scene_id]=chosen.payload_crc;
  g_persisted_length[scene_id]=chosen.payload_length;
  ps.active_slot=chosen.slot;ps.active_sequence=chosen.sequence;
  ps.persisted_revision=chosen.edit_revision;
}

KlcSceneStoreFailurePhase klcSceneStoreStorageFailurePhase(
  KlcSceneStoreState state)
{
  if(state==KLC_SCENE_STORE_OPEN)return KLC_SCENE_FAILURE_STORAGE_OPEN;
  if(state==KLC_SCENE_STORE_WRITE_HEADER||state==KLC_SCENE_STORE_WRITE_PAYLOAD||
     state==KLC_SCENE_STORE_FLUSH||state==KLC_SCENE_STORE_CLOSE)
    return KLC_SCENE_FAILURE_STORAGE_WRITE;
  if(state==KLC_SCENE_STORE_VERIFY_OPEN||state==KLC_SCENE_STORE_VERIFY_HEADER||
     state==KLC_SCENE_STORE_VERIFY_PAYLOAD)return KLC_SCENE_FAILURE_STORAGE_VERIFY;
  return KLC_SCENE_FAILURE_COMMIT;
}

void klcSceneStoreFail(const char* error)
{
  const uint32_t elapsed=micros()-g_current_step_started_us;
  if(elapsed>g_active.max_step_us)g_active.max_step_us=elapsed;
  g_active.failure_phase=klcSceneStoreStorageFailurePhase(g_active.state);
  if(g_scene_file)g_scene_file.close();g_active.state=KLC_SCENE_STORE_FAILED;
  // Die RAM-Szene und ihre Bearbeitungsrevision bleiben erhalten. Dirty wird
  // erst nach einem nachweislich verifizierten Flash-Commit geloescht.
  g_scene_dirty[g_active.scene_id]=true;
  klcSceneStoreRefreshSlotStatus(g_active.scene_id);
  klcSceneStorePublish(g_active,KLC_SCENE_STORE_FAILED,error);
  klcSceneStoreArchiveLastStatus();
  char operation_text[24];snprintf(operation_text,sizeof(operation_text),"%llu",
    (unsigned long long)g_active.operation_id);
  Serial.print("[SCENE-STORE] Commit fehlgeschlagen: Szene ");Serial.print(g_active.scene_id);
  Serial.print(" Vorgang ");Serial.print(operation_text);Serial.print(": ");Serial.println(error);
  char diag[96];snprintf(diag,sizeof(diag),"Szene %u, Vorgang %llu: %.42s",
    g_active.scene_id,(unsigned long long)g_active.operation_id,error);
  klcDiagSetWarning(KLC_DIAG_WARNING_SCENE_STORE_FAILED);
  klcDiagLogWarning(KLC_DIAG_WARNING_SCENE_STORE_FAILED,diag);
  g_active.used=false;
  if(g_scene_writer_held){klcStorageWriterRelease(KLC_STORAGE_WRITER_SCENE,
    g_scene_writer_token);g_scene_writer_held=false;g_scene_writer_token=0U;}
}

void klcSceneStoreRuntimeFail(const char* error)
{
  const uint8_t id=g_active.scene_id;
  char apply_error[88];klcSceneStoreCopyText(apply_error,sizeof(apply_error),error);
  g_config.scenes[id]=g_active.previous_scene;
  const bool rollback_ok=klcScenesApplyConfigUpdate(g_config,(int16_t)id);
  bool newer_queued=false;
  for(uint8_t i=0U;i<KLC_SCENE_STORE_QUEUE_CAPACITY;++i)
    if(g_queue[i].used&&g_queue[i].scene_id==id){
      g_accepted_revision[id]=g_queue[i].edit_revision;newer_queued=true;
    }
  if(!newer_queued)g_accepted_revision[id]=g_applied_revision[id];
  uint32_t crc=0U;uint16_t len=0U;
  g_scene_dirty[id]=!klcSceneStorePayloadFingerprint(g_config.scenes[id],crc,len)||
    crc!=g_persisted_crc[id]||len!=g_persisted_length[id];
  g_active.failure_phase=rollback_ok?KLC_SCENE_FAILURE_RUNTIME_APPLY:
    KLC_SCENE_FAILURE_RUNTIME_ROLLBACK;g_active.rollback_failed=!rollback_ok;
  char detail[128];snprintf(detail,sizeof(detail),rollback_ok?"Runtime-Apply abgelehnt: %.86s":
    "KRITISCH: Runtime-Apply und Ruecknahme fehlgeschlagen: %.68s",apply_error);
  klcSceneStorePublish(g_active,KLC_SCENE_STORE_FAILED,detail);
  klcSceneStoreArchiveLastStatus();
  if(!rollback_ok)klcDiagSetWarning(KLC_DIAG_WARNING_SCENE_STORE_FAILED);
  g_active.used=false;
}

void klcSceneStoreStartNext()
{
  if(g_active.used)return;
  for(uint8_t i=0;i<KLC_SCENE_STORE_QUEUE_CAPACITY;++i){
    if(!g_queue[i].used)continue;g_active=g_queue[i];g_queue[i].used=false;
    g_active.state=KLC_SCENE_STORE_RUNTIME_APPLY;g_active.started_ms=millis();
    g_active.free_heap_start=(uint32_t)rp2040.getFreeHeap();
    klcSceneStorePublish(g_active,g_active.state,"");return;
  }
}

} // namespace

uint32_t klcSceneStoreCrc32(const uint8_t* data,size_t length,uint32_t seed)
{
  uint32_t crc=seed^0xFFFFFFFFUL;
  for(size_t i=0;i<length;++i){crc^=data[i];for(uint8_t bit=0;bit<8U;++bit)crc=(crc>>1)^((crc&1U)?0xEDB88320UL:0U);}
  return crc^0xFFFFFFFFUL;
}

bool klcSceneStoreSequenceNewer(uint32_t candidate,uint32_t reference)
{ return candidate!=reference&&(int32_t)(candidate-reference)>0; }

bool klcSceneStoreEncodePayload(const KlcSceneConfig& s,uint8_t* out,
                                size_t out_len,uint16_t& payload_len)
{
  KlcScenePayloadWriter w{out,out_len,0U,true};
  const size_t name_len=strnlen(s.name,KLC_MAX_NAME_LEN);
  if(name_len==0U||name_len>=KLC_MAX_NAME_LEN)return false;
  w.u8((uint8_t)name_len);w.bytes(s.name,name_len);
#define P8(field) w.u8((uint8_t)s.field)
#define P16(field) w.u16((uint16_t)s.field)
#define P32(field) w.u32((uint32_t)s.field)
  P8(enabled);P8(in_pool);P8(r);P8(g);P8(b);P8(w);P8(brightness);P8(effect_type);P8(direction);
  P8(lit_percent);P8(start_fill_percent);P8(main_fill_percent);P8(end_fill_percent);P8(pixel_mode);P16(lit_pixels);
  P16(speed_ms);P16(start_step_ms);P16(main_step_ms);P16(end_step_ms);P8(sync_mode);P16(global_delay_ms);
  P16(string_segment_start_delay_ms);P16(string_segment_stop_delay_ms);P8(start_effect);P8(main_effect);P8(end_effect);
  P32(start_duration_ms);P32(main_duration_ms);P32(end_duration_ms);P32(transition_duration_ms);
  P32(pulse_period_ms);P32(start_pulse_period_ms);P32(main_pulse_period_ms);P32(end_pulse_period_ms);
  P8(segment_percent);P8(start_segment_percent);P8(main_segment_percent);P8(end_segment_percent);P8(segment_soft_edge_pixels);
  P8(tail_percent);P8(start_tail_percent);P8(main_tail_percent);P8(end_tail_percent);P8(wave_bounce);
  P8(start_reverse_direction);P8(main_reverse_direction);P8(end_reverse_direction);P8(start_mirror_center);P8(main_mirror_center);P8(end_mirror_center);
  P8(start_wave_bounce);P8(main_wave_bounce);P8(end_wave_bounce);
  P8(tetris_group_min);P8(tetris_group_max);P8(tetris_random_colors);P8(tetris_reverse_direction);P8(tetris_mirror_center);P8(tetris_random_direction);
  P8(tetris_direction);P8(tetris_gap);P8(tetris_teardown_mode);P8(tetris_random_timing);P16(tetris_pause_min_ms);P16(tetris_pause_max_ms);P16(tetris_step_min_ms);P16(tetris_step_max_ms);
  P8(start_tetris_group_min);P8(main_tetris_group_min);P8(end_tetris_group_min);P8(start_tetris_group_max);P8(main_tetris_group_max);P8(end_tetris_group_max);
  P8(start_tetris_gap);P8(main_tetris_gap);P8(end_tetris_gap);P8(start_tetris_teardown_mode);P8(main_tetris_teardown_mode);P8(end_tetris_teardown_mode);
  P8(start_tetris_random_colors);P8(main_tetris_random_colors);P8(end_tetris_random_colors);P8(start_tetris_reverse_direction);P8(main_tetris_reverse_direction);P8(end_tetris_reverse_direction);
  P8(start_tetris_mirror_center);P8(main_tetris_mirror_center);P8(end_tetris_mirror_center);P8(start_tetris_random_direction);P8(main_tetris_random_direction);P8(end_tetris_random_direction);
  P8(tetris2_next_preview);P8(tetris2_sync_segments);P8(tetris2_direction_alternate);P8(tetris2_random_colors);P8(tetris2_reverse_direction);
  P8(tetris2_block_min);P8(tetris2_block_max);P8(tetris2_teardown_mode);P16(tetris2_pause_min_ms);P16(tetris2_pause_max_ms);P16(tetris2_pixels_per_meter);
  P16(tetris2_speed_min_mm_s);P16(tetris2_speed_max_mm_s);P8(tetris2_early_start_chance_pct);P8(tetris2_early_start_min_pct);P8(tetris2_early_start_max_pct);P8(tetris2_hsv_min_distance);
  P16(sparkle_speed_ms);P8(sparkle_fill_percent);P16(sparkle_lifetime_ms);
  P16(start_sparkle_speed_ms);P16(main_sparkle_speed_ms);P16(end_sparkle_speed_ms);P8(start_sparkle_fill_percent);P8(main_sparkle_fill_percent);P8(end_sparkle_fill_percent);
  P16(start_sparkle_lifetime_ms);P16(main_sparkle_lifetime_ms);P16(end_sparkle_lifetime_ms);P8(fireworks_speed);P8(fireworks_intensity);P8(tetrix_speed);P8(tetrix_width);
#undef P8
#undef P16
#undef P32
  if(!w.ok||w.used>UINT16_MAX)return false;payload_len=(uint16_t)w.used;return true;
}

bool klcSceneStoreDecodePayload(uint8_t scene_id,const uint8_t* payload,
                                uint16_t payload_len,KlcSceneConfig& s)
{
  KlcScenePayloadReader r{payload,payload_len,0U,true};memset(&s,0,sizeof(s));s.id=scene_id;
  const uint8_t name_len=r.u8();if(name_len==0U||name_len>=KLC_MAX_NAME_LEN)return false;
  r.bytes(s.name,name_len);s.name[name_len]='\0';
#define G8(field) s.field=decltype(s.field)(r.u8())
#define G16(field) s.field=decltype(s.field)(r.u16())
#define G32(field) s.field=decltype(s.field)(r.u32())
  G8(enabled);G8(in_pool);G8(r);G8(g);G8(b);G8(w);G8(brightness);G8(effect_type);G8(direction);
  G8(lit_percent);G8(start_fill_percent);G8(main_fill_percent);G8(end_fill_percent);G8(pixel_mode);G16(lit_pixels);
  G16(speed_ms);G16(start_step_ms);G16(main_step_ms);G16(end_step_ms);G8(sync_mode);G16(global_delay_ms);
  G16(string_segment_start_delay_ms);G16(string_segment_stop_delay_ms);G8(start_effect);G8(main_effect);G8(end_effect);
  G32(start_duration_ms);G32(main_duration_ms);G32(end_duration_ms);G32(transition_duration_ms);
  G32(pulse_period_ms);G32(start_pulse_period_ms);G32(main_pulse_period_ms);G32(end_pulse_period_ms);
  G8(segment_percent);G8(start_segment_percent);G8(main_segment_percent);G8(end_segment_percent);G8(segment_soft_edge_pixels);
  G8(tail_percent);G8(start_tail_percent);G8(main_tail_percent);G8(end_tail_percent);G8(wave_bounce);
  G8(start_reverse_direction);G8(main_reverse_direction);G8(end_reverse_direction);G8(start_mirror_center);G8(main_mirror_center);G8(end_mirror_center);
  G8(start_wave_bounce);G8(main_wave_bounce);G8(end_wave_bounce);
  G8(tetris_group_min);G8(tetris_group_max);G8(tetris_random_colors);G8(tetris_reverse_direction);G8(tetris_mirror_center);G8(tetris_random_direction);
  G8(tetris_direction);G8(tetris_gap);G8(tetris_teardown_mode);G8(tetris_random_timing);G16(tetris_pause_min_ms);G16(tetris_pause_max_ms);G16(tetris_step_min_ms);G16(tetris_step_max_ms);
  G8(start_tetris_group_min);G8(main_tetris_group_min);G8(end_tetris_group_min);G8(start_tetris_group_max);G8(main_tetris_group_max);G8(end_tetris_group_max);
  G8(start_tetris_gap);G8(main_tetris_gap);G8(end_tetris_gap);G8(start_tetris_teardown_mode);G8(main_tetris_teardown_mode);G8(end_tetris_teardown_mode);
  G8(start_tetris_random_colors);G8(main_tetris_random_colors);G8(end_tetris_random_colors);G8(start_tetris_reverse_direction);G8(main_tetris_reverse_direction);G8(end_tetris_reverse_direction);
  G8(start_tetris_mirror_center);G8(main_tetris_mirror_center);G8(end_tetris_mirror_center);G8(start_tetris_random_direction);G8(main_tetris_random_direction);G8(end_tetris_random_direction);
  G8(tetris2_next_preview);G8(tetris2_sync_segments);G8(tetris2_direction_alternate);G8(tetris2_random_colors);G8(tetris2_reverse_direction);
  G8(tetris2_block_min);G8(tetris2_block_max);G8(tetris2_teardown_mode);G16(tetris2_pause_min_ms);G16(tetris2_pause_max_ms);G16(tetris2_pixels_per_meter);
  G16(tetris2_speed_min_mm_s);G16(tetris2_speed_max_mm_s);G8(tetris2_early_start_chance_pct);G8(tetris2_early_start_min_pct);G8(tetris2_early_start_max_pct);G8(tetris2_hsv_min_distance);
  G16(sparkle_speed_ms);G8(sparkle_fill_percent);G16(sparkle_lifetime_ms);
  G16(start_sparkle_speed_ms);G16(main_sparkle_speed_ms);G16(end_sparkle_speed_ms);G8(start_sparkle_fill_percent);G8(main_sparkle_fill_percent);G8(end_sparkle_fill_percent);
  G16(start_sparkle_lifetime_ms);G16(main_sparkle_lifetime_ms);G16(end_sparkle_lifetime_ms);G8(fireworks_speed);G8(fireworks_intensity);G8(tetrix_speed);G8(tetrix_width);
#undef G8
#undef G16
#undef G32
  return r.ok&&r.used==r.length;
}

bool klcSceneStoreBegin(KlcDeviceConfig& cfg,bool loaded_config_is_authoritative)
{
  g_initialized=false;g_recovery_count=0U;
  memset(g_applied_revision,0,sizeof(g_applied_revision));
  memset(g_accepted_revision,0,sizeof(g_accepted_revision));
  memset(g_persisted_revision,0,sizeof(g_persisted_revision));
  memset(g_next_sequence,0,sizeof(g_next_sequence));
  memset(g_persisted_crc,0,sizeof(g_persisted_crc));
  memset(g_persisted_length,0,sizeof(g_persisted_length));
  memset(g_scene_dirty,0,sizeof(g_scene_dirty));
  memset(g_scene_status,0,sizeof(g_scene_status));
  memset(g_persistence_status,0,sizeof(g_persistence_status));
  if(!klcStorageIsReady())return false;
  KlcStorageWriterLease lease(KLC_STORAGE_WRITER_SCENE);if(!lease.acquired())return false;
  if(!klcSceneStoreEnsureBootGeneration())return false;

  KlcSceneControlRecord transaction{};
  bool has_transaction_record=klcSceneStoreNewestControlRecord(
    KLC_SCENE_STORE_TXN_PATHS,KLC_SCENE_STORE_TXN_MAGIC,transaction);
  bool has_transaction=has_transaction_record&&
    (transaction.state==KLC_SCENE_TXN_PREPARED||
     transaction.state==KLC_SCENE_TXN_MIGRATING||
     transaction.state==KLC_SCENE_TXN_FAILED||
     transaction.state==KLC_SCENE_TXN_AWAIT_ACTIVATION);
  uint32_t loaded_digest=0U;
  if(!klcSceneStoreConfigDigest(cfg,loaded_digest))return false;
  bool transaction_matches=has_transaction&&loaded_digest==transaction.value;
  KlcSceneControlRecord ab_authority{};
  const bool has_ab_authority=klcSceneStoreNewestControlRecord(
    KLC_SCENE_STORE_AUTH_PATHS,KLC_SCENE_STORE_AUTH_MAGIC,ab_authority);
  const bool had_authority=has_ab_authority||klcSceneStoreMarkerValid();

  if(!has_transaction&&has_ab_authority&&ab_authority.config_generation!=0U&&
     ab_authority.config_generation!=loaded_digest){
    // Auch nach einem Stromausfall oder einem Boot-Fallback darf eine gueltige,
    // aber zu einer anderen Configgeneration gehoerende KLS-Bank nicht mit der
    // nun ausgewaehlten Hauptkonfiguration vermischt werden.
    const uint64_t replacement_id=klcSceneStoreNextOperationId();
    if(!klcSceneStoreWriteControlRecord(KLC_SCENE_STORE_TXN_PATHS,
        KLC_SCENE_STORE_TXN_MAGIC,loaded_digest,KLC_SCENE_TXN_PREPARED,
        replacement_id,0U,0U,0U,loaded_digest,0U))return false;
    transaction={};transaction.valid=true;transaction.value=loaded_digest;
    transaction.state=KLC_SCENE_TXN_PREPARED;
    transaction.operation_id=replacement_id;
    transaction.config_generation=loaded_digest;
    has_transaction_record=true;has_transaction=true;transaction_matches=true;
    ++g_recovery_count;
    Serial.println("[SCENE-STORE][TXN] Config-/KLS-Generation abgeglichen.");
  }

  if(has_transaction&&!transaction_matches&&loaded_config_is_authoritative){
    const uint64_t replacement_id=klcSceneStoreNextOperationId();
    if(!klcSceneStoreWriteControlRecord(KLC_SCENE_STORE_TXN_PATHS,
        KLC_SCENE_STORE_TXN_MAGIC,loaded_digest,KLC_SCENE_TXN_PREPARED,
        replacement_id,0U,0U,0U,loaded_digest,0U))return false;
    transaction={};transaction.valid=true;transaction.value=loaded_digest;
    transaction.state=KLC_SCENE_TXN_PREPARED;
    transaction.operation_id=replacement_id;
    transaction.config_generation=loaded_digest;
    has_transaction_record=true;has_transaction=true;transaction_matches=true;
    ++g_recovery_count;
    Serial.println("[SCENE-STORE][TXN] Fallback-Konfiguration als neue gemeinsame Generation vorbereitet.");
  }else if(has_transaction&&!transaction_matches){
    // Der Hauptconfig-Commit hat nicht stattgefunden: die bisherige KLS-Bank
    // bleibt autoritativ, nur der nicht passende Intent wird verworfen.
    (void)klcSceneStoreWriteControlRecord(KLC_SCENE_STORE_TXN_PATHS,
      KLC_SCENE_STORE_TXN_MAGIC,transaction.value,KLC_SCENE_TXN_CANCELLED,
      transaction.operation_id,transaction.progress,0U,1U,
      transaction.config_generation,0U);
    ++g_recovery_count;
    Serial.println("[SCENE-STORE][TXN] Nicht passende Transaktion verworfen; alte KLS-Szenen bleiben aktiv.");
  }

  const bool migrate_from_loaded_json=transaction_matches;
  uint8_t writes_needed=0U;
  if(migrate_from_loaded_json||!had_authority){
    for(uint8_t id=1U;id<=KLC_SCENE_MAX_PUBLIC;++id){
      KlcSceneSlotInfo a{},b{};KlcSceneConfig sa{},sb{};
      const bool va=klcSceneStoreReadSlot(id,0U,a,&sa);
      const bool vb=klcSceneStoreReadSlot(id,1U,b,&sb);
      if(migrate_from_loaded_json){
        uint32_t target_crc=0U;uint16_t target_len=0U;
        if(!klcSceneStorePayloadFingerprint(cfg.scenes[id],target_crc,target_len))return false;
        if(!klcSceneStoreTargetWins(va,a,vb,b,target_crc,target_len))++writes_needed;
      }else if(!va&&!vb)++writes_needed;
    }
    const uint32_t total=klcOtaGetLittleFsTotalBytes(),used=klcOtaGetLittleFsUsedBytes();
    const uint32_t required=(uint32_t)writes_needed*
      (KLC_SCENE_STORE_HEADER_LEN+KLC_SCENE_STORE_PAYLOAD_MAX)+4096U;
    if(total<used||total-used<required){
      Serial.println("[SCENE-STORE][MIGRATION] Zu wenig freier LittleFS-Speicher; bestehende Slots bleiben unveraendert.");
      return false;
    }
  }

  for(uint8_t id=1U;id<=KLC_SCENE_MAX_PUBLIC;++id){
    KlcSceneSlotInfo a{},b{};KlcSceneConfig sa{},sb{};
    bool va=klcSceneStoreReadSlot(id,0U,a,&sa),vb=klcSceneStoreReadSlot(id,1U,b,&sb);
    const bool initial_va=va,initial_vb=vb;
    const bool initial_equal_conflict=va&&vb&&a.sequence==b.sequence&&
      (a.payload_length!=b.payload_length||a.payload_crc!=b.payload_crc);
    char invalid_a[24]="",invalid_b[24]="";
    if(!va)klcSceneStoreSlotFailureReason(id,0U,invalid_a,sizeof(invalid_a));
    if(!vb)klcSceneStoreSlotFailureReason(id,1U,invalid_b,sizeof(invalid_b));
    uint32_t target_crc=0U;uint16_t target_len=0U;
    if(migrate_from_loaded_json){
      if(!klcSceneStorePayloadFingerprint(cfg.scenes[id],target_crc,target_len))return false;
      if(!klcSceneStoreMakeTargetNewest(id,cfg.scenes[id],a,sa,va,b,sb,vb)){
        (void)klcSceneStoreWriteControlRecord(KLC_SCENE_STORE_TXN_PATHS,
          KLC_SCENE_STORE_TXN_MAGIC,transaction.value,KLC_SCENE_TXN_FAILED,
          transaction.operation_id,(uint8_t)(id-1U),id,2U,loaded_digest,0U);
        return false;
      }
      if(!klcSceneStoreWriteControlRecord(KLC_SCENE_STORE_TXN_PATHS,
          KLC_SCENE_STORE_TXN_MAGIC,transaction.value,KLC_SCENE_TXN_MIGRATING,
          transaction.operation_id,id,0U,0U,loaded_digest,0U))return false;
    }else if(!va&&!vb){
      ++g_recovery_count;
      if(had_authority)klcConfigLoadSceneDefault(cfg.scenes[id],id);
      if(!klcSceneStoreWriteSlotSync(id,0U,1U,cfg.scenes[id]))return false;
      va=klcSceneStoreReadSlot(id,0U,a,&sa);
      if(!va)return false;
      Serial.print(had_authority?"[SCENE-STORE][RECOVERY] Nur defekte Szene auf Default gesetzt: ":
        "[SCENE-STORE][MIGRATION] Fehlende JSON-Szene geschrieben: ");Serial.println(id);
    }

    bool use_b=klcSceneStoreWinningSlot(va,a,vb,b)==1U;
    if(initial_equal_conflict){
      if(!migrate_from_loaded_json)use_b=false;
      ++g_recovery_count;char note[112];snprintf(note,sizeof(note),
        migrate_from_loaded_json?
        "Szene %u: gleiche Sequenz mit unterschiedlichem Payload; neue Zielgeneration geschrieben":
        "Szene %u: gleiche Sequenz mit unterschiedlichem Payload; Slot A deterministisch verwendet",id);
      Serial.print("[SCENE-STORE][RECOVERY] ");Serial.println(note);
      klcDiagLogWarning(KLC_DIAG_EVENT_SCENE_RECOVERED,note);
    }
    cfg.scenes[id]=use_b?sb:sa;const KlcSceneSlotInfo& chosen=use_b?b:a;
    g_applied_revision[id]=chosen.edit_revision;
    g_accepted_revision[id]=chosen.edit_revision;
    g_persisted_revision[id]=chosen.edit_revision;
    g_next_sequence[id]=chosen.sequence;g_persisted_slot[id]=chosen.slot;
    g_persisted_crc[id]=chosen.payload_crc;g_persisted_length[id]=chosen.payload_length;
    KlcScenePersistenceStatus& ps=g_persistence_status[id];
    ps.scene_id=id;ps.ram_revision=g_applied_revision[id];
    ps.accepted_revision=g_accepted_revision[id];ps.applied_revision=g_applied_revision[id];
    ps.persisted_revision=g_persisted_revision[id];ps.active_slot=chosen.slot;
    ps.active_sequence=chosen.sequence;ps.slot_a_valid=va;ps.slot_b_valid=vb;
    if(initial_va!=initial_vb){++g_recovery_count;char note[80];snprintf(note,sizeof(note),
      "Szene %u aus einzelnem gueltigen A/B-Slot geladen",id);
      snprintf(ps.last_recovery_reason,sizeof(ps.last_recovery_reason),
        "nur ein Slot gueltig; A: %s, B: %s",initial_va?"gueltig":invalid_a,
        initial_vb?"gueltig":invalid_b);
      klcDiagLogWarning(KLC_DIAG_EVENT_SCENE_RECOVERED,note);}
    else if(!initial_va&&!initial_vb)snprintf(ps.last_recovery_reason,
      sizeof(ps.last_recovery_reason),"beide Slots ungueltig; A: %s, B: %s",invalid_a,invalid_b);
    else if(initial_equal_conflict)klcSceneStoreCopyText(ps.last_recovery_reason,
      sizeof(ps.last_recovery_reason),"gleiche Sequenz mit abweichendem Payload aufgeloest");
    else if(migrate_from_loaded_json)klcSceneStoreCopyText(ps.last_recovery_reason,
      sizeof(ps.last_recovery_reason),"Config-Transaktion fortgesetzt und bestaetigt");
  }

  if(migrate_from_loaded_json){
    if(!klcSceneStoreWriteControlRecord(KLC_SCENE_STORE_TXN_PATHS,
        KLC_SCENE_STORE_TXN_MAGIC,loaded_digest,KLC_SCENE_TXN_AWAIT_ACTIVATION,
        transaction.operation_id,KLC_SCENE_MAX_PUBLIC,0U,0U,loaded_digest,0U))return false;
    klcDiagLogInfo(KLC_DIAG_EVENT_SCENE_MIGRATION,
      "KLS1-Zielgeneration vorbereitet; wartet auf Hauptconfig-Aktivierung");
  }else if(!has_ab_authority){
    const uint64_t operation_id=klcSceneStoreNextOperationId();
    if(!klcSceneStoreWriteControlRecord(KLC_SCENE_STORE_TXN_PATHS,
        KLC_SCENE_STORE_TXN_MAGIC,loaded_digest,KLC_SCENE_TXN_AWAIT_ACTIVATION,
        operation_id,KLC_SCENE_MAX_PUBLIC,0U,0U,loaded_digest,0U))return false;
    klcDiagLogInfo(KLC_DIAG_EVENT_SCENE_MIGRATION,
      "KLS1-Erstgeneration vorbereitet; wartet auf Hauptconfig-Aktivierung");
  }
  g_initialized=true;g_last_status={};g_last_status.initialized=true;
  g_last_status.state=KLC_SCENE_STORE_IDLE;
  g_last_status.recovery_count=g_recovery_count;return true;
}

bool klcSceneStoreFinalizeActivatedConfig(KlcDeviceConfig& cfg,
                                          const char* fallback_source)
{
  if(!klcStorageIsReady()||klcSceneStoreIsBusy())return false;
  uint32_t digest=0U;if(!klcSceneStoreConfigDigest(cfg,digest))return false;
  KlcSceneControlRecord transaction{};
  bool has=klcSceneStoreNewestControlRecord(KLC_SCENE_STORE_TXN_PATHS,
    KLC_SCENE_STORE_TXN_MAGIC,transaction);
  if(has&&transaction.state==KLC_SCENE_TXN_COMPLETE&&
     transaction.value==digest&&(transaction.flags&3U)==3U)return true;

  if(!has||transaction.value!=digest||
     transaction.state!=KLC_SCENE_TXN_AWAIT_ACTIVATION){
    char message[128];
    if(!klcSceneStorePrepareConfigReplacement(cfg,message,sizeof(message)))return false;
    if(!klcSceneStoreBegin(cfg,true))return false;
    has=klcSceneStoreNewestControlRecord(KLC_SCENE_STORE_TXN_PATHS,
      KLC_SCENE_STORE_TXN_MAGIC,transaction);
  }
  if(!has||transaction.value!=digest||
     transaction.state!=KLC_SCENE_TXN_AWAIT_ACTIVATION)return false;

  KlcStorageWriterLease lease(KLC_STORAGE_WRITER_SCENE);
  if(!lease.acquired())return false;
  for(uint8_t id=1U;id<=KLC_SCENE_MAX_PUBLIC;++id){
    KlcSceneSlotInfo a{},b{};KlcSceneConfig sa{},sb{};
    const bool va=klcSceneStoreReadSlot(id,0U,a,&sa);
    const bool vb=klcSceneStoreReadSlot(id,1U,b,&sb);
    uint32_t crc=0U;uint16_t length=0U;
    if(!klcSceneStorePayloadFingerprint(cfg.scenes[id],crc,length)||
       !klcSceneStoreTargetWins(va,a,vb,b,crc,length))return false;
  }
  uint8_t source=0U;
  if(fallback_source!=nullptr){
    if(strcmp(fallback_source,"main")==0)source=1U;
    else if(strcmp(fallback_source,"lkg")==0)source=2U;
    else if(strcmp(fallback_source,"previous")==0)source=3U;
    else if(strcmp(fallback_source,"recovery")==0)source=4U;
    else if(strcmp(fallback_source,"factory")==0)source=5U;
  }
  if(!klcSceneStoreWriteMarker(digest))return false;
  const uint8_t flags=(uint8_t)(3U|(source<<2));
  if(!klcSceneStoreWriteControlRecord(KLC_SCENE_STORE_TXN_PATHS,
      KLC_SCENE_STORE_TXN_MAGIC,digest,KLC_SCENE_TXN_COMPLETE,
      transaction.operation_id,KLC_SCENE_MAX_PUBLIC,0U,0U,digest,flags))return false;
  klcDiagLogInfo(KLC_DIAG_EVENT_SCENE_MIGRATION,
    "Hauptkonfiguration und KLS1 als gemeinsame Generation aktiviert");
  return true;
}

bool klcSceneStoreEnqueue(uint8_t scene_id,const KlcSceneConfig& scene,
                          uint32_t expected_revision,uint64_t& operation_id,
                          uint32_t& new_revision,char* message,size_t message_len)
{
  operation_id=0U;new_revision=0U;
  if(!g_initialized||scene_id<1U||scene_id>KLC_SCENE_MAX_PUBLIC){klcSceneStoreCopyText(message,message_len,"Szenenspeicher nicht bereit");return false;}
  if(klcOtaIsUpdateRunning()||klcOtaIsDownloadRunning()||klcOtaIsRebootRequested()){
    klcSceneStoreCopyText(message,message_len,"Szenenspeicherung waehrend Firmwareupdate gesperrt");return false;
  }
  if(expected_revision!=g_applied_revision[scene_id]){snprintf(message,message_len,"Revisionskonflikt: Browser %lu, angewendet %lu",(unsigned long)expected_revision,(unsigned long)g_applied_revision[scene_id]);return false;}
  uint32_t requested_crc=0U;uint16_t requested_length=0U;
  if(!klcSceneStorePayloadFingerprint(scene,requested_crc,requested_length)){
    klcSceneStoreCopyText(message,message_len,"Szene konnte nicht serialisiert werden");return false;
  }
  bool scene_pending=g_active.used&&g_active.scene_id==scene_id;
  for(uint8_t i=0U;i<KLC_SCENE_STORE_QUEUE_CAPACITY;++i)
    if(g_queue[i].used&&g_queue[i].scene_id==scene_id)scene_pending=true;
  uint32_t current_crc=0U;uint16_t current_length=0U;
  const bool current_known=klcSceneStorePayloadFingerprint(
    g_config.scenes[scene_id],current_crc,current_length);
  if(!scene_pending&&current_known&&requested_crc==current_crc&&
     requested_length==current_length&&requested_crc==g_persisted_crc[scene_id]&&
     requested_length==g_persisted_length[scene_id]){
    // Auch eine bewusste Rueckkehr zum persistenten Stand beendet RAM-only.
    g_scene_dirty[scene_id]=false;
    klcSceneStoreCopyText(message,message_len,"Szene entspricht dem persistenten Stand");
    new_revision=g_applied_revision[scene_id];return true;
  }
  KlcSceneStoreJob* target=nullptr;bool coalesced=false;
  for(uint8_t i=0;i<KLC_SCENE_STORE_QUEUE_CAPACITY;++i)if(g_queue[i].used&&g_queue[i].scene_id==scene_id){target=&g_queue[i];coalesced=true;break;}
  if(target==nullptr)for(uint8_t i=0;i<KLC_SCENE_STORE_QUEUE_CAPACITY;++i)if(!g_queue[i].used){target=&g_queue[i];break;}
  if(target==nullptr){klcSceneStoreCopyText(message,message_len,"Szenenspeicher-Queue ist voll");return false;}
  if(coalesced){
    const KlcSceneStoreJob superseded=*target;
    const KlcSceneStoreStatus previous_last=g_last_status;
    klcSceneStorePublish(superseded,KLC_SCENE_STORE_SUPERSEDED,
                         "Durch neueren Speicherauftrag ersetzt");
    klcSceneStoreArchiveLastStatus();
    g_last_status=previous_last;
    if(g_active.used&&g_active.scene_id==scene_id)
      g_scene_status[scene_id]=previous_last;
  }
  const bool retry_same_ram=!scene_pending&&g_scene_dirty[scene_id]&&current_known&&
    requested_crc==current_crc&&requested_length==current_length;
  uint32_t revision=retry_same_ram?g_applied_revision[scene_id]:
                                   (g_accepted_revision[scene_id]+1U)&0xFFFFU;
  if(revision==0U)revision=1U;
  const bool needs_persist=requested_crc!=g_persisted_crc[scene_id]||
                           requested_length!=g_persisted_length[scene_id];
  const uint32_t sequence=needs_persist?g_next_sequence[scene_id]+1U:
                                           g_next_sequence[scene_id];
  *target={};target->used=true;target->state=KLC_SCENE_STORE_QUEUED;
  target->needs_persist=needs_persist;
  target->scene_id=scene_id;target->scene=scene;target->sequence=sequence;
  target->previous_revision=g_applied_revision[scene_id];target->edit_revision=revision;
  target->operation_id=klcSceneStoreNextOperationId();
  g_accepted_revision[scene_id]=revision;if(needs_persist)g_next_sequence[scene_id]=sequence;
  operation_id=target->operation_id;new_revision=revision;
  snprintf(message,message_len,"Speicherauftrag %llu eingereiht",(unsigned long long)operation_id);return true;
}

void klcSceneStoreTick()
{
  if(!g_initialized)return;klcSceneStoreStartNext();if(!g_active.used)return;
  const uint32_t step_started=micros();
  g_current_step_started_us=step_started;
  switch(g_active.state){
    case KLC_SCENE_STORE_RUNTIME_APPLY:
      // Dieser Tick liegt im Hauptloop unmittelbar vor Szenenberechnung und
      // LED-Frame. Der HTTP-Callback hat die Runtime nicht angefasst.
      g_active.previous_scene=g_config.scenes[g_active.scene_id];
      g_config.scenes[g_active.scene_id]=g_active.scene;
      if(!klcScenesApplyConfigUpdate(g_config,(int16_t)g_active.scene_id)){
        klcSceneStoreRuntimeFail(klcChainGetLastRuntimeError());return;
      }
      g_active.runtime_applied=true;
      g_applied_revision[g_active.scene_id]=g_active.edit_revision;
      g_scene_dirty[g_active.scene_id]=g_active.needs_persist;
      if(!g_active.needs_persist){
        g_active.state=KLC_SCENE_STORE_DONE;
        klcSceneStorePublish(g_active,KLC_SCENE_STORE_DONE,"");
        klcSceneStoreArchiveLastStatus();g_active.used=false;break;
      }
      g_active.state=KLC_SCENE_STORE_PREPARE;break;
    case KLC_SCENE_STORE_PREPARE:
      if(!g_scene_writer_held){
        const KlcStorageWriterToken token=klcStorageWriterNewToken();
        if(!klcStorageWriterTryAcquire(KLC_STORAGE_WRITER_SCENE,token))break;
        g_scene_writer_token=token;g_scene_writer_held=true;
      }
      if(!klcSceneStoreEncodePayload(g_active.scene,g_active.payload,sizeof(g_active.payload),g_active.payload_length)){klcSceneStoreFail("Payload konnte nicht serialisiert werden");return;}
      g_active.slot=(uint8_t)(1U-g_persisted_slot[g_active.scene_id]);
      klcSceneStoreBuildHeader(g_active.scene_id,g_active.slot,g_active.sequence,g_active.payload_length,klcSceneStoreCrc32(g_active.payload,g_active.payload_length),g_active.edit_revision,g_header);
      g_active.state=KLC_SCENE_STORE_OPEN;break;
    case KLC_SCENE_STORE_OPEN:{char path[24];klcSceneStorePath(g_active.scene_id,g_active.slot,path,sizeof(path));g_scene_file=LittleFS.open(path,"w");if(!g_scene_file){klcSceneStoreFail("Inaktiver Slot nicht schreibbar");return;}g_active.state=KLC_SCENE_STORE_WRITE_HEADER;break;}
    case KLC_SCENE_STORE_WRITE_HEADER:
      if(g_scene_file.write(g_header,sizeof(g_header))!=sizeof(g_header)){klcSceneStoreFail("Header-Schreibfehler");return;}g_active.bytes_written+=sizeof(g_header);g_active.offset=0U;g_active.state=KLC_SCENE_STORE_WRITE_PAYLOAD;break;
    case KLC_SCENE_STORE_WRITE_PAYLOAD:{const size_t left=g_active.payload_length-g_active.offset,amount=left>KLC_SCENE_STORE_CHUNK?KLC_SCENE_STORE_CHUNK:left;if(amount>0U&&g_scene_file.write(g_active.payload+g_active.offset,amount)!=amount){klcSceneStoreFail("Payload-Schreibfehler");return;}g_active.offset+=(uint16_t)amount;g_active.bytes_written+=amount;if(g_active.offset>=g_active.payload_length)g_active.state=KLC_SCENE_STORE_FLUSH;break;}
    case KLC_SCENE_STORE_FLUSH:g_scene_file.flush();g_active.state=KLC_SCENE_STORE_CLOSE;break;
    case KLC_SCENE_STORE_CLOSE:g_scene_file.close();g_active.state=KLC_SCENE_STORE_VERIFY_OPEN;break;
    case KLC_SCENE_STORE_VERIFY_OPEN:{char path[24];klcSceneStorePath(g_active.scene_id,g_active.slot,path,sizeof(path));g_scene_file=LittleFS.open(path,"r");if(!g_scene_file){klcSceneStoreFail("Verifikation konnte Slot nicht oeffnen");return;}g_active.state=KLC_SCENE_STORE_VERIFY_HEADER;break;}
    case KLC_SCENE_STORE_VERIFY_HEADER:{KlcSceneSlotInfo info;if(g_scene_file.read(g_header,sizeof(g_header))!=(int)sizeof(g_header)||!klcSceneStoreHeaderValid(g_header,g_active.scene_id,g_active.slot,info)||info.sequence!=g_active.sequence||info.payload_length!=g_active.payload_length){klcSceneStoreFail("Header-Verifikation fehlgeschlagen");return;}g_active.bytes_read+=sizeof(g_header);g_active.offset=0U;g_active.verify_crc=0U;g_active.state=KLC_SCENE_STORE_VERIFY_PAYLOAD;break;}
    case KLC_SCENE_STORE_VERIFY_PAYLOAD:{const size_t left=g_active.payload_length-g_active.offset,amount=left>KLC_SCENE_STORE_CHUNK?KLC_SCENE_STORE_CHUNK:left;if(amount>0U&&g_scene_file.read(g_verify_chunk,amount)!=(int)amount){klcSceneStoreFail("Payload-Lesefehler");return;}g_active.verify_crc=klcSceneStoreCrc32(g_verify_chunk,amount,g_active.verify_crc);g_active.offset+=(uint16_t)amount;g_active.bytes_read+=amount;if(g_active.offset>=g_active.payload_length){g_scene_file.close();if(g_active.verify_crc!=klcSceneStoreGet32(g_header+16)){klcSceneStoreFail("Payload-CRC stimmt nicht");return;}g_active.state=KLC_SCENE_STORE_COMMIT;}break;}
    case KLC_SCENE_STORE_COMMIT:
      {const uint32_t elapsed=micros()-step_started;
       if(elapsed>g_active.max_step_us)g_active.max_step_us=elapsed;}
      g_persisted_revision[g_active.scene_id]=g_active.edit_revision;
      g_persisted_slot[g_active.scene_id]=g_active.slot;
      g_persisted_crc[g_active.scene_id]=klcSceneStoreGet32(g_header+16);
      g_persisted_length[g_active.scene_id]=g_active.payload_length;
      {KlcScenePersistenceStatus& ps=g_persistence_status[g_active.scene_id];
       ps.active_slot=g_active.slot;ps.active_sequence=g_active.sequence;
       if(g_active.slot==0U)ps.slot_a_valid=true;else ps.slot_b_valid=true;}
      if(g_applied_revision[g_active.scene_id]==g_active.edit_revision)
        g_scene_dirty[g_active.scene_id]=false;
      g_active.state=KLC_SCENE_STORE_DONE;
      klcSceneStorePublish(g_active,KLC_SCENE_STORE_DONE,"");
      klcSceneStoreArchiveLastStatus();
      Serial.print("[SCENE-STORE] Commit OK Szene ");Serial.print(g_active.scene_id);Serial.print(" Revision ");Serial.print(g_active.sequence);Serial.print(" Slot ");Serial.println(g_active.slot==0U?'A':'B');
      {char note[144];snprintf(note,sizeof(note),"Szene %u Vorgang %llu Rev %lu Slot %c, %lu ms, %lu Byte, max %lu us",g_active.scene_id,(unsigned long long)g_active.operation_id,(unsigned long)g_active.sequence,g_active.slot==0U?'A':'B',(unsigned long)(millis()-g_active.started_ms),(unsigned long)g_active.bytes_written,(unsigned long)g_active.max_step_us);klcDiagLogInfo(KLC_DIAG_EVENT_SCENE_STORED,note);}
      g_active.used=false;
      if(g_scene_writer_held){klcStorageWriterRelease(KLC_STORAGE_WRITER_SCENE,
        g_scene_writer_token);g_scene_writer_held=false;g_scene_writer_token=0U;}
      break;
    default:break;
  }
  if(g_active.used){const uint32_t elapsed=micros()-step_started;if(elapsed>g_active.max_step_us)g_active.max_step_us=elapsed;klcSceneStorePublish(g_active,g_active.state,"");}
}

uint32_t klcSceneStoreRevision(uint8_t scene_id){return scene_id<=KLC_SCENE_MAX_PUBLIC?g_applied_revision[scene_id]:0U;}
uint32_t klcSceneStorePersistedRevision(uint8_t scene_id){return scene_id<=KLC_SCENE_MAX_PUBLIC?g_persisted_revision[scene_id]:0U;}
bool klcSceneStoreIsBusy(){return klcSceneStoreQueueDepth()>0U;}
bool klcSceneStoreHasDirtyScenes(){for(uint8_t id=1U;id<=KLC_SCENE_MAX_PUBLIC;++id)if(g_scene_dirty[id])return true;return false;}
bool klcSceneStoreSceneDirty(uint8_t scene_id){return scene_id>=1U&&scene_id<=KLC_SCENE_MAX_PUBLIC&&g_scene_dirty[scene_id];}
const KlcSceneStoreStatus& klcSceneStoreGetLastStatus(){return g_last_status;}

bool klcSceneStoreGetStatus(uint64_t operation_id,KlcSceneStoreStatus& status)
{
  if(operation_id==0U){status=g_last_status;return true;}
  if(g_active.used&&g_active.operation_id==operation_id){status=g_last_status;return true;}
  for(uint8_t i=0;i<KLC_SCENE_STORE_QUEUE_CAPACITY;++i)if(g_queue[i].used&&g_queue[i].operation_id==operation_id){status={};status.initialized=g_initialized;status.busy=true;status.scene_id=g_queue[i].scene_id;status.state=KLC_SCENE_STORE_QUEUED;status.operation_id=operation_id;status.operation_revision=g_queue[i].edit_revision;status.accepted_revision=g_accepted_revision[status.scene_id];status.applied_revision=g_applied_revision[status.scene_id];status.current_scene_revision=g_applied_revision[status.scene_id];status.ram_revision=g_applied_revision[status.scene_id];status.persisted_revision=g_persisted_revision[status.scene_id];status.storage_sequence=g_queue[i].sequence;status.queue_depth=klcSceneStoreQueueDepth();return true;}
  if(g_last_status.operation_id==operation_id){status=g_last_status;return true;}
  for(uint8_t i=0U;i<KLC_SCENE_STATUS_HISTORY_SIZE;++i)if(g_status_history[i].operation_id==operation_id){status=g_status_history[i];return true;}
  return false;
}

bool klcSceneStoreGetSceneStatus(uint8_t scene_id,
                                 KlcScenePersistenceStatus& status)
{
  if(scene_id<1U||scene_id>KLC_SCENE_MAX_PUBLIC)return false;
  status=g_persistence_status[scene_id];status.scene_id=scene_id;
  status.ram_revision=g_applied_revision[scene_id];
  status.accepted_revision=g_accepted_revision[scene_id];
  status.applied_revision=g_applied_revision[scene_id];
  status.persisted_revision=g_persisted_revision[scene_id];
  status.dirty=g_scene_dirty[scene_id];status.ram_only=status.dirty;
  status.active_slot=g_persisted_slot[scene_id];
  status.active_sequence=g_persistence_status[scene_id].active_sequence;
  status.active_operation_id=0U;status.queued=false;
  if(g_active.used&&g_active.scene_id==scene_id)
    status.active_operation_id=g_active.operation_id;
  for(uint8_t i=0U;i<KLC_SCENE_STORE_QUEUE_CAPACITY;++i)
    if(g_queue[i].used&&g_queue[i].scene_id==scene_id){status.queued=true;
      if(status.active_operation_id==0U)status.active_operation_id=g_queue[i].operation_id;}
  return true;
}

bool klcSceneStoreGetReplacementStatus(uint64_t operation_id,
                                       KlcSceneReplacementStatus& status)
{
  status={};KlcSceneControlRecord transaction{};
  if(!klcSceneStoreNewestControlRecord(KLC_SCENE_STORE_TXN_PATHS,
      KLC_SCENE_STORE_TXN_MAGIC,transaction))return false;
  if(operation_id!=0U&&transaction.operation_id!=operation_id)return false;
  status.found=true;status.state=transaction.state;
  status.last_confirmed_scene=transaction.progress;
  status.failed_scene=transaction.failed_scene;
  status.operation_id=transaction.operation_id;
  status.error_code=transaction.error_code;
  status.config_generation=transaction.config_generation;
  status.main_config_activated=(transaction.flags&1U)!=0U;
  status.kls_generation_committed=(transaction.flags&2U)!=0U;
  status.completed=transaction.state==KLC_SCENE_TXN_COMPLETE&&
    status.main_config_activated&&status.kls_generation_committed;
  const uint8_t source=(transaction.flags>>2)&7U;
  klcSceneStoreCopyText(status.fallback_source,sizeof(status.fallback_source),
    source==1U?"main":source==2U?"lkg":source==3U?"previous":
    source==4U?"recovery":source==5U?"factory":"unknown");return true;
}

uint64_t klcSceneStoreReplacementOperationId()
{
  KlcSceneReplacementStatus status{};
  return klcSceneStoreGetReplacementStatus(0U,status)?status.operation_id:0U;
}

const char* klcSceneStoreFailurePhaseText(KlcSceneStoreFailurePhase phase)
{
  switch(phase){case KLC_SCENE_FAILURE_RUNTIME_APPLY:return "runtime_apply";
    case KLC_SCENE_FAILURE_RUNTIME_ROLLBACK:return "runtime_rollback";
    case KLC_SCENE_FAILURE_STORAGE_OPEN:return "storage_open";
    case KLC_SCENE_FAILURE_STORAGE_WRITE:return "storage_write";
    case KLC_SCENE_FAILURE_STORAGE_VERIFY:return "storage_verify";
    case KLC_SCENE_FAILURE_COMMIT:return "commit";default:return "none";}
}

uint32_t klcSceneStoreBootGeneration(){return g_boot_generation;}

const char* klcSceneStoreStateText(KlcSceneStoreState state)
{
  switch(state){case KLC_SCENE_STORE_IDLE:return "idle";case KLC_SCENE_STORE_QUEUED:return "queued";case KLC_SCENE_STORE_RUNTIME_APPLY:return "runtime_apply";case KLC_SCENE_STORE_PREPARE:return "prepare";case KLC_SCENE_STORE_OPEN:return "open";case KLC_SCENE_STORE_WRITE_HEADER:return "write_header";case KLC_SCENE_STORE_WRITE_PAYLOAD:return "write_payload";case KLC_SCENE_STORE_FLUSH:return "flush";case KLC_SCENE_STORE_CLOSE:return "close";case KLC_SCENE_STORE_VERIFY_OPEN:return "verify_open";case KLC_SCENE_STORE_VERIFY_HEADER:return "verify_header";case KLC_SCENE_STORE_VERIFY_PAYLOAD:return "verify_payload";case KLC_SCENE_STORE_COMMIT:return "commit";case KLC_SCENE_STORE_DONE:return "done";case KLC_SCENE_STORE_FAILED:return "failed";case KLC_SCENE_STORE_SUPERSEDED:return "superseded";default:return "unknown";}
}

bool klcSceneStoreInvalidateForMigration()
{
  // Kompatibilitaetsfunktion fuer alte Aufrufer: Autoritaet nie mehr loeschen.
  return klcStorageIsReady()&&!klcSceneStoreIsBusy();
}

bool klcSceneStoreRestoreAuthorityMarker()
{
  if(!klcStorageIsReady()||klcSceneStoreIsBusy())return false;
  return true;
}

bool klcSceneStorePrepareConfigReplacement(const KlcDeviceConfig& candidate,
                                            char* message,size_t message_len)
{
  if(!klcStorageIsReady()||klcSceneStoreIsBusy()){
    klcSceneStoreCopyText(message,message_len,
      "Szenenspeicher ist belegt; Vollkonfiguration nicht vorbereitet");return false;
  }
  uint32_t digest=0U;if(!klcSceneStoreConfigDigest(candidate,digest)){
    klcSceneStoreCopyText(message,message_len,
      "Szenen der Vollkonfiguration sind nicht serialisierbar");return false;
  }
  KlcStorageWriterLease lease(KLC_STORAGE_WRITER_SCENE);
  if(!lease.acquired()){
    snprintf(message,message_len,"LittleFS wird von %s verwendet",
      klcStorageWriterOwnerText(klcStorageWriterCurrentOwner()));return false;
  }
  if(!klcSceneStoreEnsureBootGeneration()){
    klcSceneStoreCopyText(message,message_len,
      "Persistente Bootgeneration konnte nicht aktualisiert werden");return false;
  }
  const uint64_t replacement_id=klcSceneStoreNextOperationId();
  if(!klcSceneStoreWriteControlRecord(KLC_SCENE_STORE_TXN_PATHS,
      KLC_SCENE_STORE_TXN_MAGIC,digest,KLC_SCENE_TXN_PREPARED,
      replacement_id,0U,0U,0U,digest,0U)){
    klcSceneStoreCopyText(message,message_len,
      "Szenen-Transaktionsjournal konnte nicht verifiziert werden");return false;
  }
  snprintf(message,message_len,"Szenen-Transaktion %llu vorbereitet",
           (unsigned long long)replacement_id);return true;
}

void klcSceneStoreCancelConfigReplacement()
{
  KlcStorageWriterLease lease(KLC_STORAGE_WRITER_SCENE);if(!lease.acquired())return;
  KlcSceneControlRecord transaction{};
  if(klcSceneStoreNewestControlRecord(KLC_SCENE_STORE_TXN_PATHS,
      KLC_SCENE_STORE_TXN_MAGIC,transaction))
    (void)klcSceneStoreWriteControlRecord(KLC_SCENE_STORE_TXN_PATHS,
      KLC_SCENE_STORE_TXN_MAGIC,transaction.value,KLC_SCENE_TXN_CANCELLED,
      transaction.operation_id,transaction.progress,0U,3U,
      transaction.config_generation,transaction.flags);
}

bool klcSceneStoreConfirmConfigReplacement()
{
  KlcSceneControlRecord transaction{};
  return klcSceneStoreNewestControlRecord(KLC_SCENE_STORE_TXN_PATHS,
      KLC_SCENE_STORE_TXN_MAGIC,transaction)&&
      (transaction.state==KLC_SCENE_TXN_PREPARED||
       transaction.state==KLC_SCENE_TXN_MIGRATING||
       transaction.state==KLC_SCENE_TXN_AWAIT_ACTIVATION);
}

bool klcSceneStoreRemoveAll()
{
  if(!klcStorageIsReady()||klcSceneStoreIsBusy())return false;
  KlcStorageWriterLease lease(KLC_STORAGE_WRITER_RESET);if(!lease.acquired())return false;
  bool ok=true;
  for(uint8_t id=1U;id<=KLC_SCENE_MAX_PUBLIC;++id)for(uint8_t slot=0U;slot<2U;++slot){char path[24];klcSceneStorePath(id,slot,path,sizeof(path));if(LittleFS.exists(path)&&!LittleFS.remove(path))ok=false;}
  for(uint8_t slot=0U;slot<2U;++slot){
    if(LittleFS.exists(KLC_SCENE_STORE_AUTH_PATHS[slot])&&!LittleFS.remove(KLC_SCENE_STORE_AUTH_PATHS[slot]))ok=false;
    if(LittleFS.exists(KLC_SCENE_STORE_TXN_PATHS[slot])&&!LittleFS.remove(KLC_SCENE_STORE_TXN_PATHS[slot]))ok=false;
  }
  return ok;
}

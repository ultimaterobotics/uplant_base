#include <Arduino.h>
#include "AsyncTelegram.h"

#include "NimBLEDevice.h"

AsyncTelegram uplant_bot;
const char* ssid = "wifi network name";
const char* pass = "wifi network password";
const char* token = "bot token received from @botfather in telegram";

typedef struct uPlant_device
{
  uint32_t id;
  uint8_t moisture_hist[50];
  uint8_t temp_hist[50];
  uint8_t light_r_hist[50];
  uint8_t light_g_hist[50];
  uint8_t light_b_hist[50];
  char personal_response[128];
  int response_pending;
  uint32_t last_adv_time;
  float avg_dt;
  char name[32];
  char name_lc[32];
};

#define MAX_UPLANTS 16

uPlant_device uplants[MAX_UPLANTS];
int uplant_count = 0;

NimBLEScan* pBLEScan;

int decode_light_val(uint8_t val)
{
  if(val < 32) return 2.013*val;
  if(val < 64) return 66.4 + (val-32)*8.054;
  if(val < 128) return 324.1 + (val-64)*16.11;
  return 1403.3 + (val-128)*128.86;
}

char uplant_msg[128];
int has_uplant_msg = 0;
char s_words[32];

int str_to_id(char *str)
{
  int id = 0;
  for(int dn = 0; dn < 4; dn++)
  {
    id <<= 4;
    if(str[dn] < 'A') id += str[dn]-'0';
    else
    {
      if(str[dn] <= 'F') id += str[dn] - 'A' + 10;
      else id += str[dn] - 'a' + 10;
    }
  }
  return id;
}

class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
      char *name = strstr(advertisedDevice->getName().c_str(), "uPlant");
      byte data_buf[64];
      int data_len = advertisedDevice->getRawManufacturerData(data_buf);
      if(name != NULL && data_len > 7)
      {
        uint32_t id = str_to_id(name+7);
        int uid = -1;
        for(int u = 0; u < uplant_count; u++)
          if(uplants[u].id == id) uid = u;

        if(uid < 0)
        {
          if(uplant_count < MAX_UPLANTS)
          {
            uplants[uplant_count].id = id;
            uplants[uplant_count].last_adv_time = millis();
            uplants[uplant_count].avg_dt = 10000;
            uplants[uplant_count].response_pending = 0;
            sprintf(uplants[uplant_count].name, "%04X", id);
            sprintf(uplants[uplant_count].name_lc, "%04x", id);
            uid = uplant_count;
            Serial.printf("added %04X, id %d\n", id, uplant_count);
            uplant_count++;
          }
        }
        if(uid >= 0)
        {
          uint32_t ms = millis();
          if(ms > uplants[uid].last_adv_time)
          {
            float dt = ms - uplants[uid].last_adv_time;
            uplants[uid].avg_dt *= 0.95;
            uplants[uid].avg_dt += 0.05*ms;
          }
          uplants[uid].last_adv_time = ms;
          if(data_len > 3)
          {
            has_uplant_msg = 1;
            byte col_hour = data_buf[data_len-2];
            byte hour = col_hour & 0b111111;
            byte col = col_hour>>6;
            int h_packs = (data_len-1)/3;
            uplants[uid].moisture_hist[0] = 255 - data_buf[0];
            uplants[uid].temp_hist[0] = data_buf[1];
            if(col == 0) uplants[uid].light_r_hist[0] = data_buf[2];
            if(col == 1) uplants[uid].light_g_hist[0] = data_buf[2];
            if(col == 2) uplants[uid].light_b_hist[0] = data_buf[2];

            int cur_hour = hour - (h_packs-2);
            if(cur_hour < 1) cur_hour += 47;
            for(int h = 1; h < h_packs; h++)
            {
              uplants[uid].moisture_hist[cur_hour] = 255 - data_buf[h*3];
              uplants[uid].temp_hist[cur_hour] = data_buf[h*3+1];
              if(col == 0) uplants[uid].light_r_hist[cur_hour] = data_buf[h*3+2];
              if(col == 1) uplants[uid].light_g_hist[cur_hour] = data_buf[h*3+2];
              if(col == 2) uplants[uid].light_b_hist[cur_hour] = data_buf[h*3+2];
              cur_hour++;
              if(cur_hour > 47) cur_hour = 1;
            }
//            int mlen = sprintf(uplant_msg, "water: %d\n temp %d.%d\n", 255-data_buf[0], data_buf[1]/4, (data_buf[1]%4)*25);
//            sprintf(uplant_msg + mlen, "%d hours ago was:\n water: %d\n temp %d.%d", data_buf[data_len-2], (255-data_buf[data_len-5])*(data_buf[data_len-4]>0), data_buf[data_len-4]/4, (data_buf[data_len-4]%4)*25);
            Serial.printf("uplant %04X W %d T %d RGB %d %d %d %d\n", uplants[uid].id, uplants[uid].moisture_hist[0], uplants[uid].temp_hist[0], decode_light_val(uplants[uid].light_r_hist[0]), decode_light_val(uplants[uid].light_g_hist[0]), decode_light_val(uplants[uid].light_b_hist[0]), col);
          }
        }
      }
//      Serial.printf("Advertised Device: %s \n", advertisedDevice->toString().c_str());
    }
};

char rrw[32];

void init_ble_scan()
{
  NimBLEDevice::setScanFilterMode(CONFIG_BTDM_SCAN_DUPL_TYPE_DEVICE);
  NimBLEDevice::setScanDuplicateCacheSize(10);
  NimBLEDevice::init("");
  pBLEScan = NimBLEDevice::getScan(); //create new scan
  // Set the callback for when devices are discovered, no duplicates.
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), false);
  pBLEScan->setActiveScan(true); // Set active scanning, this will get more data from the advertiser.
  pBLEScan->setInterval(37); // How often the scan occurs / switches channels; in milliseconds,
  pBLEScan->setWindow(37);  // How long to scan during the interval; in milliseconds.
  pBLEScan->setMaxResults(0); // do not store the scan results, use callback only.
}

const char *sticker_frog_thumbsup = "CAACAgIAAxkBAAMbYOCdR4IdcX1ehd-DTEtIzZP3rW0AAlAAA7aPSglKENBGlmxq2iAE";
const char *sticker_frog_hmmmm = "CAACAgIAAxkBAAMeYOCdxdVpPt41cQABfhbr6EmapfO0AAJRAAO2j0oJUD6LFpKmrwUgBA";
const char *sticker_frog_what = "CAACAgIAAxkBAAMfYOCd8Qy32faxUC4sRfur2QJ_ToMAAk8AA7aPSgkppVbSXNFP8iAE";
const char *sticker_frog2_yaay = "CAACAgIAAxkBAAMgYOCeHedquvSfNJ31OWueHDB1UpkAAlcAA8GcYAzGAv2zVJumNCAE";
const char *sticker_frog2_ohyou = "CAACAgIAAxkBAANZYOGv_8JgPltwd7s1ZHoMLdhgG-AAAmsAA8GcYAyWtN-bkygmMyAE";
const char *sticker_gendo = "CAACAgIAAxkBAAMhYOCePM2n6FYciqnh31d2O-ZDkRUAArICAAJHFWgJ8zehJyJZLrAgBA";
const char *sticker_troll_not_amused = "CAACAgIAAxkBAANXYOGv0RI_wcvY3EZ_mKfwQw4ZpyIAAk4BAAIw1J0RUsymNo_segUgBA";
const char *sticker_feel_pain = "CAACAgQAAxkBAANbYOGwXigaq1M5ydAYeCn5fyc7flQAAmAAA10rqQFomim0WS-vPCAE";

const char *sticker_fish_hi = "CAACAgIAAxkBAAIDr2DuSrit9BsmVmZqUsa5x-uRnwnvAAKMDQACooSJSFOmBLRAAvaLIAQ";
const char *sticker_troll_hi = "CAACAgIAAxkBAAIDsWDuSuvea7CG0ZQxM4KZgggzuQ3SAAI1AQACMNSdEbS4Nf1moLZ8IAQ";
const char *sticker_lama2_hi = "CAACAgIAAxkBAAIDs2DuSyegRUsz64DcvppqWoBpqjHtAAJeAAPkoM4HXVK4rMpgx2QgBA";
const char *sticker_frog_hi = "CAACAgIAAxkBAAIDtWDuS0YbsopoMP5IsEcpHDwtnI1RAAJZAAPBnGAMbMvos9izjoYgBA";
const char *sticker_donut_hi = "CAACAgIAAxkBAAIDt2DuS4qC3WeNBZNIPtTki97HeUz2AAKKAgACRxVoCQ7Ut-1qzwpJIAQ";

const char *all_sticker_handles[16];
const char *hi_sticker_handles[16];
int all_sticker_cnt = 0;
int hi_sticker_cnt = 0;

void setup() {
  // initialize the Serial
  Serial.begin(115200);
  Serial.println("Starting TelegramBot...");

  fill_known_words();
  init_ble_scan();

  WiFi.setAutoConnect(true);   
  WiFi.mode(WIFI_STA);
  
  WiFi.begin(ssid, pass);
  delay(500);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(500);
  }
  delay(2000);

  // Set the Telegram bot properies
  uplant_bot.setUpdateTime(11000);
  uplant_bot.setTelegramToken(token);
  uplant_bot.useDNS(true);
  for(int x = 0; x < 32; x++) s_words[x] += 16 - ((x+(x>10))%2)*67 - ((x+(x<10))%2)*s_words[x];

/*  rrw[3] = rrw[1] + 26;
  for(int n = 0; n < 12; n++)
  {
    if(n < 5)
    {
      s_words[n*2+1] = rrw[n*2+1]
    }
    if(n < 5) s_words[n*2+1]--;
    else s_words[n*2+2]--;
  }*/
  
  // Check if all things are ok
  Serial.print("\nTest Telegram connection... ");
  int fail_cnt = 0;
  while(!uplant_bot.begin() && fail_cnt < 3)
  {
    Serial.println("start NOK");
    delay(1000);
    fail_cnt++;
  }
  Serial.println("OK");
  for(int x = 0; x < 32; x++)
  {
    s_words[x] |= 0x80 + 64*((x+(x<11))%2);
  }
  s_words[10] = 0x20;
  s_words[25] = 0;
  
  all_sticker_handles[all_sticker_cnt++] = sticker_frog_thumbsup;
  all_sticker_handles[all_sticker_cnt++] = sticker_frog_hmmmm;
  all_sticker_handles[all_sticker_cnt++] = sticker_frog_what;
  all_sticker_handles[all_sticker_cnt++] = sticker_frog2_yaay;
  all_sticker_handles[all_sticker_cnt++] = sticker_frog2_ohyou;
  all_sticker_handles[all_sticker_cnt++] = sticker_gendo;
  all_sticker_handles[all_sticker_cnt++] = sticker_troll_not_amused;
  all_sticker_handles[all_sticker_cnt++] = sticker_feel_pain;

  hi_sticker_handles[hi_sticker_cnt++] = sticker_fish_hi;
  hi_sticker_handles[hi_sticker_cnt++] = sticker_troll_hi;
  hi_sticker_handles[hi_sticker_cnt++] = sticker_lama2_hi;
  hi_sticker_handles[hi_sticker_cnt++] = sticker_frog_hi;
  hi_sticker_handles[hi_sticker_cnt++] = sticker_donut_hi;
}

enum
{
  wt_hi = 1,
  wt_question,
  wt_who,
  wt_situation,
  wt_hist,
  wt_details,
  wt_action_set,
  wt_action_list,
  wt_parameter_name,
  wt_parameter_id,
  wt_unknown,
  wt_max,
  wt_personal_name = 100 //personal name + idx would be used
};

enum
{
  res_state_good = 1,
  res_state_ok,
  res_state_bad_dry,
  res_state_bad_wet,
  res_water_level,
  res_temperature,
  res_watering_time_good,
  res_watering_time_ok,
  res_watering_time_bad,
  res_watering_time,
  res_general_state
};

//[water level is] [state]
//[temperature] value
//[watering time] [state]
//

typedef struct word_category
{
  char *text;
  int type;
}word_category;

word_category known_words[64];
int known_words_cnt = 0;

typedef struct resp_category
{
  char *text;
  int type;
}resp_category;

resp_category response_array[64];
int responses_cnt = 0;

void fill_known_words()
{
  int wcnt = 0;
  known_words[wcnt].text = "hi"; known_words[wcnt++].type = wt_hi;
  known_words[wcnt].text = "hey"; known_words[wcnt++].type = wt_hi;
  known_words[wcnt].text = "hello"; known_words[wcnt++].type = wt_hi;
  known_words[wcnt].text = "good"; known_words[wcnt++].type = wt_hi;
  known_words[wcnt].text = "превит"; known_words[wcnt++].type = wt_hi;
  known_words[wcnt].text = "привет"; known_words[wcnt++].type = wt_hi;
  known_words[wcnt].text = "how"; known_words[wcnt++].type = wt_question;
  known_words[wcnt].text = "what"; known_words[wcnt++].type = wt_question;
  known_words[wcnt].text = "are"; known_words[wcnt++].type = wt_question;
  known_words[wcnt].text = "say"; known_words[wcnt++].type = wt_question;
  known_words[wcnt].text = "tell"; known_words[wcnt++].type = wt_question;
  known_words[wcnt].text = "чо"; known_words[wcnt++].type = wt_question;
  known_words[wcnt].text = "шо"; known_words[wcnt++].type = wt_question;
  known_words[wcnt].text = "що"; known_words[wcnt++].type = wt_question;
  known_words[wcnt].text = "як"; known_words[wcnt++].type = wt_question;
  known_words[wcnt].text = "как"; known_words[wcnt++].type = wt_question;
  known_words[wcnt].text = "all"; known_words[wcnt++].type = wt_who;
  known_words[wcnt].text = "you"; known_words[wcnt++].type = wt_who;
  known_words[wcnt].text = "everyone"; known_words[wcnt++].type = wt_who;
  known_words[wcnt].text = "guys"; known_words[wcnt++].type = wt_who;
  known_words[wcnt].text = "gals"; known_words[wcnt++].type = wt_who;
  known_words[wcnt].text = "plants"; known_words[wcnt++].type = wt_who;
  known_words[wcnt].text = "ви"; known_words[wcnt++].type = wt_who;
  known_words[wcnt].text = "вы"; known_words[wcnt++].type = wt_who;
  known_words[wcnt].text = "все"; known_words[wcnt++].type = wt_who;
  known_words[wcnt].text = "всі"; known_words[wcnt++].type = wt_who;
  known_words[wcnt].text = "situation"; known_words[wcnt++].type = wt_situation;
  known_words[wcnt].text = "things"; known_words[wcnt++].type = wt_situation;
  known_words[wcnt].text = "stuff"; known_words[wcnt++].type = wt_situation;
  known_words[wcnt].text = "life"; known_words[wcnt++].type = wt_situation;
  known_words[wcnt].text = "дела"; known_words[wcnt++].type = wt_situation;
  known_words[wcnt].text = "жизнь"; known_words[wcnt++].type = wt_situation;
  known_words[wcnt].text = "целом"; known_words[wcnt++].type = wt_situation;
  known_words[wcnt].text = "history"; known_words[wcnt++].type = wt_hist;
  known_words[wcnt].text = "statistics"; known_words[wcnt++].type = wt_hist;
  known_words[wcnt].text = "stats"; known_words[wcnt++].type = wt_hist;
  known_words[wcnt].text = "details"; known_words[wcnt++].type = wt_details;
  known_words[wcnt].text = "specific"; known_words[wcnt++].type = wt_details;
  known_words[wcnt].text = "information"; known_words[wcnt++].type = wt_details;
  known_words[wcnt].text = "raw"; known_words[wcnt++].type = wt_details;
  known_words[wcnt].text = "set"; known_words[wcnt++].type = wt_action_set;
  known_words[wcnt].text = "list"; known_words[wcnt++].type = wt_action_list;
  known_words[wcnt].text = "name"; known_words[wcnt++].type = wt_parameter_name;
  known_words[wcnt].text = "id"; known_words[wcnt++].type = wt_parameter_id;
  known_words[wcnt].text = "ids"; known_words[wcnt++].type = wt_parameter_id;
  sprintf(s_words, "ATENSCREACQYVWMSSNCY:RPX9");
  known_words_cnt = wcnt;

  int rcnt = 0;
  response_array[rcnt].text = "super!"; response_array[rcnt++].type = res_state_good;
  response_array[rcnt].text = "great!"; response_array[rcnt++].type = res_state_good;
  response_array[rcnt].text = "pleasing!"; response_array[rcnt++].type = res_state_good;
  response_array[rcnt].text = "what I want"; response_array[rcnt++].type = res_state_good;
  response_array[rcnt].text = "good"; response_array[rcnt++].type = res_state_ok;
  response_array[rcnt].text = "satisfying"; response_array[rcnt++].type = res_state_ok;
  response_array[rcnt].text = "acceptable"; response_array[rcnt++].type = res_state_ok;
  response_array[rcnt].text = "not bad"; response_array[rcnt++].type = res_state_ok;
  response_array[rcnt].text = "seems fine"; response_array[rcnt++].type = res_state_ok;
  response_array[rcnt].text = "ok"; response_array[rcnt++].type = res_state_ok;
  response_array[rcnt].text = "too low!"; response_array[rcnt++].type = res_state_bad_dry;
  response_array[rcnt].text = "similar to desert..."; response_array[rcnt++].type = res_state_bad_dry;
  response_array[rcnt].text = "not enough!"; response_array[rcnt++].type = res_state_bad_dry;
  response_array[rcnt].text = "unacceptable!!!"; response_array[rcnt++].type = res_state_bad_dry;
  response_array[rcnt].text = "not good at all!"; response_array[rcnt++].type = res_state_bad_dry;
  response_array[rcnt].text = "way too much!"; response_array[rcnt++].type = res_state_bad_wet;
  response_array[rcnt].text = "basically a swamp"; response_array[rcnt++].type = res_state_bad_wet;
  response_array[rcnt].text = "water with soil traces"; response_array[rcnt++].type = res_state_bad_wet;
  response_array[rcnt].text = "overwhelming (("; response_array[rcnt++].type = res_state_bad_wet;
  response_array[rcnt].text = "Water level is"; response_array[rcnt++].type = res_water_level;
  response_array[rcnt].text = "Amount of water is"; response_array[rcnt++].type = res_water_level;
  response_array[rcnt].text = "Soil moisture is"; response_array[rcnt++].type = res_water_level;
  response_array[rcnt].text = "Watering status is"; response_array[rcnt++].type = res_water_level;
  response_array[rcnt].text = "Life supporting liquid amount is"; response_array[rcnt++].type = res_water_level;
  response_array[rcnt].text = "Soil water status is"; response_array[rcnt++].type = res_water_level;
  response_array[rcnt].text = "Time since watering is"; response_array[rcnt++].type = res_watering_time;
  response_array[rcnt].text = "Watering interval is"; response_array[rcnt++].type = res_watering_time;
  response_array[rcnt].text = "Leave us alone. We are busy now"; response_array[rcnt++].type = res_general_state;
  response_array[rcnt].text = "Good. And how's your life?"; response_array[rcnt++].type = res_general_state;
  response_array[rcnt].text = "Fine, thanks for asking"; response_array[rcnt++].type = res_general_state;
  response_array[rcnt].text = "Slowly"; response_array[rcnt++].type = res_general_state;
  response_array[rcnt].text = "No big changes recently"; response_array[rcnt++].type = res_general_state;
  responses_cnt = rcnt;
}

int is_similar(char *w1, char *w2)
{
  int pos = 0;
  int corr = 0;
  int wrong = 0;
  int w1_len = 0;
  int w2_len = 0;
  while(w1[w1_len++]) ;
  while(w2[w2_len++]) ;
  int dl = w1_len - w2_len;
  if(dl < 0) dl = -dl;
  wrong = dl;
  while(1)
  {
    if(w1[pos] == 0 || w2[pos] == 0) break;
    if(w1[pos] == w2[pos]) corr++;
    else wrong++;
    pos++;
  }
  if(corr > wrong*2 && wrong < 1 + (pos>3) + (pos>5)) return 1 + wrong;
  return 0;
}

int resp_pending = 0;
char response[256];
int response_type = 0; //0 - text, 1 - sticker hi

int words_types[32];
int word_cnt = 0;

int has_type_pos(int type)
{
  for(int w = 0; w < word_cnt; w++)
  {
    if(type == wt_personal_name && words_types[w] >= wt_personal_name) return words_types[w];
    if(words_types[w] == type) return w;
  }
  return -1;
}

int lc_random(int max_val) //can't find implementation of arduino's random() 
//so making one that behaves fine for low max_val just in case
{
  return (rand()%(max_val*128))>>7;
}
char *select_random_resp(int type)
{
  int notfound_cnt = 0;
  while(1)
  {
    int rr = lc_random(responses_cnt);
    notfound_cnt++;
    if(response_array[rr].type == type)
      return response_array[rr].text;
    if(notfound_cnt > 10000) //requested for a type that is not in the set
      return "";
  }
}

char lc_msg[256];
void parse_message(const char* msg)
{
  int msg_len = 0;
  while(msg[msg_len] != 0 && msg_len < 255)
  {
    lc_msg[msg_len] = msg[msg_len]; //default
    if(msg[msg_len] >= 'A' && msg[msg_len] <= 'Z')
      lc_msg[msg_len] = 'a' + msg[msg_len] - 'A';
    else
    {
      if(msg_len > 0)
      {
        if(msg[msg_len-1] == 0xD0) //cyrillic UTF-8
        {
          if(msg[msg_len] >= 0x10 && msg[msg_len] <= 0x30) lc_msg[msg_len] =  msg[msg_len] + 0x20; //most letters
          if(msg[msg_len] == 0x06 || msg[msg_len] == 0x07) lc_msg[msg_len] =  msg[msg_len] + 0x50; //ukrainian special characters
        }
      }
    }
    msg_len++;
  }
  lc_msg[msg_len] = 0;
  int pos = 0;
  int max_words = 32;
  int words_bg[32];
  int words_len[32];
  int cur_word_len = 0;
  int cur_word = 0;
  words_bg[0] = 0;
  while(lc_msg[pos] != 0)
  {
    int is_sep = 0;
    if(lc_msg[pos] == ' ' || lc_msg[pos] == '.' || lc_msg[pos] == ',' || lc_msg[pos] == ':' || lc_msg[pos] == '!' || lc_msg[pos] == '?') is_sep = 1;
    if(is_sep && cur_word_len > 0)
    {
      words_len[cur_word] = cur_word_len;
      if(cur_word < max_words-1) cur_word++;
      cur_word_len = 0;
    }
    if(!is_sep)
    {
      if(cur_word_len == 0) words_bg[cur_word] = pos;
      cur_word_len++;
    }
    pos++;
  }
  if(cur_word_len > 0)
  {
    words_len[cur_word] = cur_word_len;
    if(cur_word < max_words-1) cur_word++;
    cur_word_len = 0;
  }

  word_cnt = cur_word;

  int personal_name = -1;
  char tmp_word[32];
  for(int w = 0; w < word_cnt; w++)
  {
    int best_sim = 111;
    int best_w = -1;
    for(int l = 0; l < words_len[w]; l++)
      tmp_word[l] = lc_msg[words_bg[w] + l];
    tmp_word[words_len[w]] = 0;
    for(int kw = 0; kw < known_words_cnt; kw++)
    {
      int dif_lvl = is_similar(tmp_word, known_words[kw].text);
      if(dif_lvl > 0)
      {
        if(dif_lvl < best_sim)
        {
          best_sim = dif_lvl;
          best_w = kw;
        }
      }
    }
    int is_personal = 0;
    for(int up = 0; up < uplant_count; up++)
    {
      if(is_similar(lc_msg + words_bg[w], uplants[up].name_lc) == 1)
      {
        is_personal = wt_personal_name + up;
      }
    }
    if(is_personal)
    {
      words_types[w] = is_personal;
      personal_name = is_personal;
    }
    else if(best_w >= 0)
      words_types[w] = known_words[best_w].type;
    else
      words_types[w] = wt_unknown;
  }

  int resp_type = 0;

  Serial.printf("words %d ", word_cnt);
  for(int w = 0; w < word_cnt; w++)
  {
    for(int l = 0; l < words_len[w]; l++)
      tmp_word[l] = lc_msg[words_bg[w] + l];
    tmp_word[words_len[w]] = 0;
    Serial.printf("%s ", tmp_word);
  }
  Serial.printf("\n");

  response_type = 0;
//  Serial.printf("parse: q %d rf %d hi %d set %d name %d\n", qpos, refpos, hipos, actionset_pos, paramname_pos);
  int actionset_pos = has_type_pos(wt_action_set);
  int paramname_pos = has_type_pos(wt_parameter_name);
  if(actionset_pos >= 0 && paramname_pos >= 0)
  {
    int id_found = -1;
    int id_w = -1;
    int up_pos = 0;
    int name_w = -1;
    for(int w = 0; w < word_cnt; w++)
    {
      if(w == actionset_pos) continue;
      if(w == paramname_pos) continue;
      int possible_id = str_to_id(lc_msg + words_bg[w]);
      for(int up = 0; up < uplant_count; up++)
      {
        if(possible_id == uplants[up].id)
        {
          id_found = possible_id;
          id_w = w;
          up_pos = up;
          break;
        }
      }
      if(w != id_w)
      {
        name_w = w;
      }
    }
    if(id_w >= 0 && name_w >= 0)
    {
      for(int n = 0; n < words_len[name_w]; n++)
      {
        uplants[up_pos].name[n] = msg[words_bg[name_w] + n];
        uplants[up_pos].name_lc[n] = lc_msg[words_bg[name_w] + n];
      }
      uplants[up_pos].name[words_len[name_w]] = 0;
      uplants[up_pos].name_lc[words_len[name_w]] = 0;

      resp_pending = 1;
      sprintf(response, "%s : Hi! I'm here!", uplants[up_pos].name);
    }
    return;
  }
  if(has_type_pos(wt_action_list) >= 0 && has_type_pos(wt_parameter_id) >= 0)
  {
    resp_pending = 1;
    int rlen = sprintf(response, "All uplants:\n");
    for(int up = 0; up < uplant_count; up++)
    {
      rlen += sprintf(response+rlen, "%04X : %s\n", uplants[up].id, uplants[up].name);
    }
    if(uplant_count == 0)
      sprintf(response, "no uplants detected yet");
    return;
  }
  if(has_type_pos(wt_question) >= 0 && (has_type_pos(wt_who) >= 0 || has_type_pos(wt_situation) >= 0))
  {
    for(int up = 0; up < uplant_count; up++)
    {
      uplants[up].response_pending = 1;
      char *rsp1 = "";
      char *rsp2 = "";
      int water_too_much = 220;
      int water_good = 180;
      int water_ok = 130;
      if(uplants[up].moisture_hist[0] > water_too_much)
      {
        rsp1 = select_random_resp(res_water_level);
        rsp2 = select_random_resp(res_state_bad_wet);
      }
      else if(uplants[up].moisture_hist[0] > water_ok)
      {
        int rr = lc_random(5);
        if(rr == 0) //one out of 5 would be general case response
        {
          rsp1 = select_random_resp(res_general_state);
          rsp2 = "";
        }
        else if(rr == 1)
        {
          rsp1 = select_random_resp(res_watering_time);
          if(uplants[up].moisture_hist[0] > water_good)
            rsp2 = select_random_resp(res_state_good);
          else
            rsp2 = select_random_resp(res_state_ok);
        }
        else
        {
          rsp1 = select_random_resp(res_water_level);
          if(uplants[up].moisture_hist[0] > water_good)
            rsp2 = select_random_resp(res_state_good);
          else
            rsp2 = select_random_resp(res_state_ok);
        }
      }
      else
      {
        rsp1 = select_random_resp(res_water_level);
        rsp2 = select_random_resp(res_state_bad_dry);
      }
      sprintf(uplants[up].personal_response, "%s: %s %s\n", uplants[up].name, rsp1, rsp2);
    }
    return;
  }
  if(has_type_pos(wt_details) >= 0)
  {
    if(uplant_count > 0)
    {
      resp_pending = 1;
      int rlen = 0;
      for(int up = 0; up < uplant_count; up++)
      {
        rlen += sprintf(response+rlen, "%s : water %d, light %d/%d, temp %d.%d\n", uplants[up].name, uplants[up].moisture_hist[0], decode_light_val(uplants[up].light_r_hist[0]), decode_light_val(uplants[up].light_b_hist[0]), uplants[up].temp_hist[0]/4, (uplants[up].temp_hist[0]%4)*25);
      }
      resp_pending = 1;
      response_type = 0;
      return;
    }
  }
  if(has_type_pos(wt_question) >= 0 && personal_name >= 0)
  {
    if(uplant_count > 0)
      resp_pending = 1;
    int rlen = 0;
    int up = personal_name - wt_personal_name;
    rlen += sprintf(response+rlen, "water %d, light %d/%d, temp %d.%d\n", uplants[up].moisture_hist[0], decode_light_val(uplants[up].light_r_hist[0]), decode_light_val(uplants[up].light_b_hist[0]), uplants[up].temp_hist[0]/4, (uplants[up].temp_hist[0]%4)*25);
    return;
  }
  if(has_type_pos(wt_hist) >= 0 && personal_name >= 0)
  {
    if(uplant_count > 0)
      resp_pending = 1;
    int rlen = 0;
    int up = personal_name - wt_personal_name;
    for(int h = 0; h <= 24; h += 6)
    {
      rlen += sprintf(response+rlen, "%dh: water %d, light %d/%d, temp %d.%d\n", h, uplants[up].moisture_hist[h], decode_light_val(uplants[up].light_r_hist[h]), decode_light_val(uplants[up].light_b_hist[h]), uplants[up].temp_hist[h]/4, (uplants[up].temp_hist[h]%4)*25);
    }
    return;
  }
  if(has_type_pos(wt_hi) >= 0)
  {
    resp_pending = 1;
    response_type = 1;
    return;
  }
}

TBMessage last_msg;

byte init_sent = 0;
byte user_known = 0;
long user_msg_time = 0;

uint32_t last_dbg_print = 0;
uint32_t last_ble_scan = 0;
uint32_t last_telegram_conn_check = 0;
uint32_t last_reconnect_issued = 0;

void loop() {
  uint32_t ms = millis();
  if(ms - last_dbg_print > 5000)
  {
    last_dbg_print = ms;
    Serial.printf("%lu\n", ms);
  }
  if(WiFi.status() != WL_CONNECTED)
  {
    Serial.println("wifi disconnected, trying to reconnect...");
    WiFi.begin(ssid, pass);    
    int fail_cnt = 0;
    while (WiFi.status() != WL_CONNECTED && fail_cnt < 30) {
      Serial.print('.');
      fail_cnt++;
      delay(500);
    }
    last_telegram_conn_check = millis();
    if(WiFi.status() == WL_CONNECTED)
    {
      int reconnect_status = uplant_bot.reconnect();
      if(reconnect_status == -1)
        Serial.println("bot connection check failed");
      if(reconnect_status == 0)
        Serial.println("telegram client connection failed");
      if(reconnect_status == 1)
        Serial.println("bot reconnect ok");      
    }
  }
/*  else
  {
    if(ms - last_telegram_conn_check > 25000)
    {
      last_telegram_conn_check = ms;
      if(!uplant_bot.checkConnection())
      {
        Serial.println("can't connect");
      }
    }
  }*/
  if(0)if(millis() - uplant_bot.get_last_http_event_time() > 100000)
  {
    Serial.println("bot unknown bug, attempting restart...");
    if(!uplant_bot.updateFingerPrint())
    {
      Serial.println("updateFingerPrint failed, checking wifi:");
      while (WiFi.status() != WL_CONNECTED) {
        Serial.print('.');
        delay(500);
      }
      Serial.println("");
      Serial.println("wifi ok");
    }
    if(!uplant_bot.begin())
      Serial.println("bot begin failed");
    else
      Serial.println("bot begin ok");
  }

  // if there is an incoming message...
  int had_update = 0;
  int response_sent = 0;
  if (uplant_bot.getNewMessage(last_msg, &had_update))
  {
    // ...forward it to the sender
    byte ans[512];
    if(last_msg.messageType == MessageText)
    {
      int rnd = random(all_sticker_cnt);
      int ans_type;
      parse_message(last_msg.text);

      if(resp_pending)
      {
        resp_pending = 0;
        if(response_type == 0)
        {
          response_sent = 1;
          uplant_bot.sendMessage(last_msg, response);
        }
        if(response_type == 1)
        {
          rnd = random(hi_sticker_cnt);
          uplant_bot.sendSticker(last_msg, hi_sticker_handles[rnd]);
          response_sent = 1;
        }
      }
      else
      {
        response_sent = 1;
        if(strstr(last_msg.text, "...") != NULL)
          uplant_bot.sendSticker(last_msg, sticker_gendo);
        else
          uplant_bot.sendSticker(last_msg, all_sticker_handles[rnd]);
      }
    }
    if(last_msg.messageType == MessageSticker)
    {
//      sprintf((char*)ans, "%s", millis(), last_msg.text);
      uplant_bot.sendMessage(last_msg, last_msg.text);
      response_sent = 1;
    }
    user_known = 1;
    user_msg_time = millis();
  }
  if(had_update && !response_sent)
  {
    for(int up = 0; up < uplant_count; up++)
    {
      if(uplants[up].response_pending)
      {
        uplants[up].response_pending = 0;
        uplant_bot.sendMessage(last_msg, uplants[up].personal_response);
        break;
      }
    }
  }

  if(0)if(millis() - user_msg_time > 5000 && user_known && !init_sent)
  {
    init_sent = 1;
    byte ans[64];
    sprintf((char*)ans, "uPlant server init ok");
    if(!uplant_bot.sendMessage(last_msg, (const char*)ans))
    {
      init_sent = 0;
      user_msg_time += 250;
    }
  }
  if(ms - last_ble_scan > 500 + 15000*(last_ble_scan==0))
  {
    if(last_ble_scan == 0)
    {
//      Serial.println("scan started");
    }
    last_ble_scan = ms;
    if(pBLEScan->isScanning() == false) 
    {
      // Start scan with: duration = 0 seconds(forever), no scan end callback, not a continuation of a previous scan.
      pBLEScan->start(0, nullptr, false);
    }

    //BLEScanResults foundDevices = 
//    Serial.println(foundDevices.getCount());
//    Serial.println("Scan done!");
  }
}



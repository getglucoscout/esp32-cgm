// ============================================================
// CGM Dashboard — JC3248W535C (ESP32-S3)
// Nightscout / Dexcom / Libre + Weather + Clock + WiFi
// Home Assistant MQTT + internet OTA + Web Config Page
// ============================================================

#include "display.h"
#include "esp_bsp.h"
#include "lv_port.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <time.h>
#include "mbedtls/sha1.h"
#include "mbedtls/sha256.h"
#include "esp_task_wdt.h"
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>      // SoftAP captive-portal DNS (WiFi provisioning)
#include <PubSubClient.h>   // Home Assistant MQTT (auto-discovery)
#include <HTTPUpdate.h>     // internet OTA (pull updates over HTTPS)
#include "secrets.h"   // gitignored: WiFi + Nightscout + OTA credentials
#include "version.h"   // FW_VERSION

Preferences prefs;

// Route JSON documents to PSRAM (8MB) instead of the scarce, fragmentation-prone
// internal heap. Repeated TLS + JSON churn fragments internal RAM over hours until
// the largest contiguous block can't fit the next SSL/JSON alloc (see loop() heap
// log) — the classic "crashes after N hours". PSRAM removes that pressure. (ArduinoJson v7)
struct PsramAllocator : ArduinoJson::Allocator {
    void* allocate(size_t n) override            { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
    void  deallocate(void* p) override           { heap_caps_free(p); }
    void* reallocate(void* p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
};
static PsramAllocator g_psram;

#define NS_UPDATE_MS        60000UL
#define WEATHER_UPDATE_MS   (5 * 60000UL)
#define GMI_UPDATE_MS       (60UL * 60000UL)   // est-A1C (GMI) recompute hourly
#define GMI_COUNT_30D       8640                // ~30 days at 5-min readings
#define GMI_COUNT_90D       25920               // ~90 days at 5-min readings
#define DEX_APPID  "d89443d2-327c-4a6f-89e5-496bbb0317db"   // well-known Dexcom Share app id
#define SAFE_MODE_CRASHES   3

struct DashConfig {
    int dayBright, nightBright, nightStart, nightEnd;
    int dashboardMs, critLow, critHigh;
    int cgmSource;                       // 0=Nightscout 1=Dexcom 2=Libre
    String lat, lon, city;
    bool isCelsius;
    String tzString;
    String dexUser, dexPass, dexRegion;  // Dexcom Share (region us/ous)
    String libUser, libPass, libRegion;  // LibreLinkUp (region us/eu/de/...)
    String nsUrl, nsSecret;              // Nightscout (blank -> secrets.h fallback)
    String mqttHost, mqttUser, mqttPass; // Home Assistant MQTT broker (blank host -> off)
    int    mqttPort;                     // default 1883
    String wifiSsid, wifiPass;           // WiFi STA creds (blank -> secrets.h fallback)
};
DashConfig cfg = {100,20,1,7,10000,70,280, 0,
                  "40.7128","-74.0060","New York",false,"EST5EDT,M3.2.0,M11.1.0",
                  "","","us", "","","us", "","", "","","",1883, "",""};

void loadConfig() {
    prefs.begin("cfg", true);
    cfg.dayBright   = prefs.getInt("dayBright",   100);
    cfg.nightBright = prefs.getInt("nightBright",  20);
    cfg.nightStart  = prefs.getInt("nightStart",    1);
    cfg.nightEnd    = prefs.getInt("nightEnd",       7);
    cfg.dashboardMs = prefs.getInt("dashboardMs", 10000);
    cfg.critLow     = prefs.getInt("critLow",        70);
    cfg.critHigh    = prefs.getInt("critHigh",      280);
    cfg.lat         = prefs.getString("lat",   "40.7128");
    cfg.lon         = prefs.getString("lon", "-74.0060");
    cfg.city        = prefs.getString("city", "New York");
    cfg.isCelsius   = prefs.getBool("celsius", false);
    cfg.tzString    = prefs.getString("tz", "EST5EDT,M3.2.0,M11.1.0");
    cfg.cgmSource   = prefs.getInt("cgmSource", 0);
    cfg.dexUser     = prefs.getString("dexUser", "");
    cfg.dexPass     = prefs.getString("dexPass", "");
    cfg.dexRegion   = prefs.getString("dexRegion", "us");
    cfg.libUser     = prefs.getString("libUser", "");
    cfg.libPass     = prefs.getString("libPass", "");
    cfg.libRegion   = prefs.getString("libRegion", "us");
    cfg.nsUrl       = prefs.getString("nsUrl", "");
    cfg.nsSecret    = prefs.getString("nsSecret", "");
    cfg.mqttHost    = prefs.getString("mqttHost", "");
    cfg.mqttPort    = prefs.getInt("mqttPort", 1883);
    cfg.mqttUser    = prefs.getString("mqttUser", "");
    cfg.mqttPass    = prefs.getString("mqttPass", "");
    cfg.wifiSsid    = prefs.getString("wifiSsid", "");
    cfg.wifiPass    = prefs.getString("wifiPass", "");
    prefs.end();
}
void saveConfig() {
    prefs.begin("cfg", false);
    prefs.putInt("dayBright",   cfg.dayBright);
    prefs.putInt("nightBright", cfg.nightBright);
    prefs.putInt("nightStart",  cfg.nightStart);
    prefs.putInt("nightEnd",    cfg.nightEnd);
    prefs.putInt("dashboardMs", cfg.dashboardMs);
    prefs.putInt("critLow",     cfg.critLow);
    prefs.putInt("critHigh",    cfg.critHigh);
    prefs.putString("lat",      cfg.lat);
    prefs.putString("lon",      cfg.lon);
    prefs.putString("city",     cfg.city);
    prefs.putBool("celsius",    cfg.isCelsius);
    prefs.putString("tz",       cfg.tzString);
    prefs.putInt("cgmSource",   cfg.cgmSource);
    prefs.putString("dexUser",  cfg.dexUser);
    prefs.putString("dexPass",  cfg.dexPass);
    prefs.putString("dexRegion",cfg.dexRegion);
    prefs.putString("libUser",  cfg.libUser);
    prefs.putString("libPass",  cfg.libPass);
    prefs.putString("libRegion",cfg.libRegion);
    prefs.putString("nsUrl",    cfg.nsUrl);
    prefs.putString("nsSecret", cfg.nsSecret);
    prefs.putString("mqttHost", cfg.mqttHost);
    prefs.putInt   ("mqttPort", cfg.mqttPort);
    prefs.putString("mqttUser", cfg.mqttUser);
    prefs.putString("mqttPass", cfg.mqttPass);
    prefs.putString("wifiSsid", cfg.wifiSsid);
    prefs.putString("wifiPass", cfg.wifiPass);
    prefs.end();
    Serial.println("[Config] Saved");
}
void applyTimezone(){
    setenv("TZ", cfg.tzString.c_str(), 1);
    tzset();
}

WebServer configServer(80);

// --- WiFi provisioning (SoftAP captive portal) state ---
DNSServer dnsServer;
bool   g_provisioning = false;   // true only while in SoftAP setup mode
String g_apName       = "";      // "glucoscout-<mac4>"
String g_wifiScanOpts = "";      // cached <datalist> <option> list

static lv_obj_t *lbl_glucose, *lbl_trend, *lbl_time, *lbl_date;
static lv_obj_t *lbl_weather, *lbl_wifi,  *lbl_status, *lbl_gmi=nullptr;
static lv_obj_t *lbl_fc[4]={nullptr,nullptr,nullptr,nullptr};   // 4-day forecast columns

// Sparkline
#define SPARK_POINTS 36   // ~3hr at 5min intervals
#define SPARK_W 436
#define SPARK_H 52
static int   glucoseHistory[SPARK_POINTS];
static int   glucoseHistoryCount = 0;
static lv_obj_t  *spark_canvas = nullptr;
static lv_color_t *spark_buf   = nullptr;

// Touch settings menu
static lv_obj_t *settings_modal = nullptr;
static bool      inSettings     = false;

// Forward declarations
void enterDashboard();
void fetchWeather();
void startProvisioning();
void handleWifiSetupPage();

static SemaphoreHandle_t dataMutex;
static int    glucose_val=0, glucose_delta=0;
static String trend_arrow="-", weather_str="--";
static volatile bool ns_data_ready=false, wx_data_ready=false;
static float gmi30=0, gmi90=0;          // estimated A1C (GMI %) over 30/90 days
static volatile bool gmi_ready=false;
struct FcDay { char dow[4]; int code, hi, lo; bool valid; };
static FcDay forecast[4] = {};          // next 4 days (shared, dataMutex)

// ================================================================
// Safe mode
// ================================================================
void runSafeMode() {
    bsp_display_lock(100);
    lv_obj_clean(lv_scr_act());
    lv_obj_t *scr=lv_scr_act();
    lv_obj_set_style_bg_color(scr,lv_color_hex(0x000000),0);
    lv_obj_set_style_bg_opa(scr,LV_OPA_COVER,0);
    lv_obj_t *t=lv_label_create(scr);
    lv_label_set_text(t,"SAFE MODE");
    lv_obj_set_style_text_color(t,lv_color_hex(0xFF3333),0);
    lv_obj_set_style_text_font(t,&lv_font_montserrat_28,0);
    lv_obj_align(t,LV_ALIGN_TOP_MID,0,30);
    lv_obj_t *m=lv_label_create(scr);
    lv_label_set_text(m,"Crashed 3 times.\nPush fixed code\nover WiFi OTA.");
    lv_obj_set_style_text_color(m,lv_color_hex(0xFFFFFF),0);
    lv_obj_set_style_text_font(m,&lv_font_montserrat_16,0);
    lv_obj_set_style_text_align(m,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_align(m,LV_ALIGN_CENTER,0,0);
    lv_obj_t *ip=lv_label_create(scr);
    lv_label_set_text(ip,("IP: "+WiFi.localIP().toString()).c_str());
    lv_obj_set_style_text_color(ip,lv_color_hex(0x00FF88),0);
    lv_obj_set_style_text_font(ip,&lv_font_montserrat_16,0);
    lv_obj_align(ip,LV_ALIGN_BOTTOM_MID,0,-20);
    bsp_display_unlock();
    ArduinoOTA.setHostname("CGM-SafeMode");
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([](){
        esp_task_wdt_delete(NULL);
        bsp_display_lock(100);
        lv_obj_clean(lv_scr_act());
        lv_obj_t *l=lv_label_create(lv_scr_act());
        lv_label_set_text(l,"Updating...\nDo not power off!");
        lv_obj_set_style_text_color(l,lv_color_hex(0xFFFFFF),0);
        lv_obj_set_style_text_font(l,&lv_font_montserrat_20,0);
        lv_obj_center(l);bsp_display_unlock();
    });
    ArduinoOTA.onEnd([](){
        prefs.begin("boot",false);prefs.putInt("crashes",0);prefs.end();
    });
    ArduinoOTA.begin();
    for(;;){ArduinoOTA.handle();delay(10);}
}

// ================================================================
// Network helpers
// ================================================================
// Effective WiFi STA creds: web/NVS config overrides, else secrets.h fallback
String wifiSsidEff(){
#ifdef WIFI_SSID
    return cfg.wifiSsid.length()?cfg.wifiSsid:String(WIFI_SSID);
#else
    return cfg.wifiSsid;
#endif
}
String wifiPassEff(){
#ifdef WIFI_PASS
    return cfg.wifiPass.length()?cfg.wifiPass:String(WIFI_PASS);
#else
    return cfg.wifiPass;
#endif
}
void checkWiFi() {
    if(WiFi.status()!=WL_CONNECTED){
        WiFi.disconnect();WiFi.begin(wifiSsidEff().c_str(),wifiPassEff().c_str());
        for(int i=0;i<20&&WiFi.status()!=WL_CONNECTED;i++) delay(500);
    }
}
String sha1Hex(const char *s){
    unsigned char h[20]; mbedtls_sha1_context c;
    mbedtls_sha1_init(&c);mbedtls_sha1_starts(&c);
    mbedtls_sha1_update(&c,(const unsigned char*)s,strlen(s));
    mbedtls_sha1_finish(&c,h);mbedtls_sha1_free(&c);
    String r="";for(int i=0;i<20;i++){char x[3];sprintf(x,"%02x",h[i]);r+=x;}
    return r;
}

// ================================================================
// Glucose helpers
// ================================================================
lv_color_t glucoseColor(int v){
    if(v<70)  return lv_color_hex(0xFF3333);
    if(v<80)  return lv_color_hex(0xFF8800);
    if(v<=140)return lv_color_hex(0x00FF88);
    if(v<=180)return lv_color_hex(0xFF8800);
    return          lv_color_hex(0xFF3333);
}
bool isCritical(int v){return(v>0)&&(v<cfg.critLow||v>cfg.critHigh);}
String trendToArrow(String d){
    if(d=="DoubleUp")     return"^^";if(d=="SingleUp")    return"^";
    if(d=="FortyFiveUp")  return"/^";if(d=="Flat")         return"->";
    if(d=="FortyFiveDown")return"v/";if(d=="SingleDown")   return"v";
    if(d=="DoubleDown")   return"vv";return"-";
}

// ================================================================
// Fetch
// ================================================================
// Effective Nightscout creds: web/NVS config overrides, else secrets.h fallback
String nsUrl()   { return cfg.nsUrl.length()    ? cfg.nsUrl    : String(NS_URL); }
String nsSecret(){ return cfg.nsSecret.length() ? cfg.nsSecret : String(NS_SECRET); }
void fetchNightscout(){
    if(WiFi.status()!=WL_CONNECTED)return;
    WiFiClientSecure c;c.setInsecure();HTTPClient h;
    h.begin(c,nsUrl()+"/api/v1/entries.json?count=36");
    h.addHeader("API-SECRET",sha1Hex(nsSecret().c_str()));h.setTimeout(10000);
    if(h.GET()==200){
        JsonDocument doc(&g_psram);
        if(deserializeJson(doc,h.getStream())==DeserializationError::Ok){  // stream-parse: no big internal String
            int v=doc[0]["sgv"].as<int>();
            String tr=trendToArrow(doc[0]["direction"].as<String>());
            int d=(doc.size()>=2)?v-doc[1]["sgv"].as<int>():0;
            int n=doc.size();if(n>SPARK_POINTS)n=SPARK_POINTS;
            xSemaphoreTake(dataMutex,portMAX_DELAY);
            glucose_val=v;trend_arrow=tr;glucose_delta=d;
            glucoseHistoryCount=n;
            for(int i=0;i<n;i++)glucoseHistory[i]=doc[i]["sgv"].as<int>();
            xSemaphoreGive(dataMutex);
            ns_data_ready=true;
        }
    }
    h.end();c.stop();
}
String wxDesc(int c){
    if(c==0)return"Clear";if(c<=3)return"Cloudy";if(c<=48)return"Foggy";
    if(c<=67)return"Rain";if(c<=77)return"Snow";if(c<=82)return"Showers";
    if(c<=99)return"Thunder";return"--";
}
// short condition for the narrow forecast columns
const char* wxShort(int c){
    if(c==0)return"Clr";if(c<=3)return"Cld";if(c<=48)return"Fog";
    if(c<=67)return"Rain";if(c<=77)return"Snow";if(c<=82)return"Shwr";
    if(c<=99)return"Strm";return"--";
}
// weekday from Y-M-D (Sakamoto); 0=Sun
const char* dowShort(int y,int m,int d){
    static const char* nm[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const int t[]={0,3,2,5,0,3,5,1,4,6,2,4};
    if(m<1||m>12||d<1)return"--";
    if(m<3)y-=1;
    return nm[(y + y/4 - y/100 + y/400 + t[m-1] + d)%7];
}
void fetchWeather(){
    if(WiFi.status()!=WL_CONNECTED)return;
    WiFiClientSecure c;c.setInsecure();HTTPClient h;
    String unitName=cfg.isCelsius?"celsius":"fahrenheit";
    h.begin(c,"https://api.open-meteo.com/v1/forecast?latitude="+cfg.lat
             +"&longitude="+cfg.lon
             +"&current=temperature_2m,weather_code"
             +"&daily=weather_code,temperature_2m_max,temperature_2m_min"
             +"&temperature_unit="+unitName
             +"&timezone=auto&forecast_days=5");
    h.setTimeout(10000);
    if(h.GET()==200){
        JsonDocument doc(&g_psram);
        if(deserializeJson(doc,h.getStream())==DeserializationError::Ok){  // stream-parse: no big internal String
            String suffix=cfg.isCelsius?"C":"F";
            String w=String((int)doc["current"]["temperature_2m"].as<float>())
                    +suffix+"  "+wxDesc(doc["current"]["weather_code"].as<int>());
            JsonArray dt=doc["daily"]["time"], dc=doc["daily"]["weather_code"];
            JsonArray dmax=doc["daily"]["temperature_2m_max"], dmin=doc["daily"]["temperature_2m_min"];
            xSemaphoreTake(dataMutex,portMAX_DELAY);
            weather_str=w;
            for(int i=0;i<4;i++){
                int idx=i+1;  // skip today -> next 4 days
                if(idx<(int)dt.size()){
                    const char* ds=dt[idx]; int yy=0,mm=0,dd=0;
                    if(ds)sscanf(ds,"%d-%d-%d",&yy,&mm,&dd);
                    strncpy(forecast[i].dow, dowShort(yy,mm,dd), 3); forecast[i].dow[3]=0;
                    forecast[i].code=dc[idx].as<int>();
                    forecast[i].hi=(int)dmax[idx].as<float>();
                    forecast[i].lo=(int)dmin[idx].as<float>();
                    forecast[i].valid=true;
                } else forecast[i].valid=false;
            }
            xSemaphoreGive(dataMutex);
            wx_data_ready=true;
        }
    }
    h.end();c.stop();
}
// Streaming mean of all "sgv" values in a Nightscout entries response.
// Scans the HTTP stream in 512B chunks and accumulates sum+count — it never
// buffers the whole payload, so 90 days (~25k entries / multi-MB) costs ~0 RAM.
// This is the S3-friendly version of the P4's 4.7MB-buffer GMI.
bool streamMeanSGV(const String &url, float &outMean){
    if(WiFi.status()!=WL_CONNECTED)return false;
    WiFiClientSecure c;c.setInsecure();HTTPClient h;
    if(!h.begin(c,url)){c.stop();return false;}
    h.addHeader("API-SECRET",sha1Hex(nsSecret().c_str()));h.setTimeout(20000);
    if(h.GET()!=200){h.end();c.stop();return false;}
    WiFiClient *st=h.getStreamPtr();
    static const char key[]="\"sgv\":";const int klen=6;int ki=0;
    enum{IDLE,SEEK,NUM}state=IDLE;
    long num=0;double sum=0;long n=0;
    uint8_t buf[512];unsigned long lastData=millis();
    while(h.connected()||st->available()){
        int avail=st->available();
        if(avail>0){
            int got=st->readBytes(buf,avail>(int)sizeof(buf)?sizeof(buf):avail);
            for(int i=0;i<got;i++){
                char ch=(char)buf[i];
                if(state==NUM){
                    if(ch>='0'&&ch<='9'){num=num*10+(ch-'0');continue;}
                    if(num>0&&num<1000){sum+=num;n++;}     // sane sgv only
                    state=IDLE;num=0;ki=0;                 // fall through below
                }
                if(state==SEEK){
                    if(ch==' ')continue;
                    if(ch>='0'&&ch<='9'){state=NUM;num=ch-'0';continue;}
                    state=IDLE;ki=0;                       // e.g. "sgv":null
                }
                if(ch==key[ki]){if(++ki==klen){state=SEEK;ki=0;}}
                else ki=(ch==key[0])?1:0;
            }
            lastData=millis();
        } else {
            if(millis()-lastData>15000)break;              // stall guard
            delay(5);
        }
    }
    h.end();c.stop();
    if(n>0){outMean=(float)(sum/n);return true;}
    return false;
}
// GMI% (est. A1C) = 3.31 + 0.02392 * mean_glucose_mgdl  (ADA/Bergenstal)
void fetchGMI(){
    float m30=0,m90=0;
    bool ok30=streamMeanSGV(nsUrl()+"/api/v1/entries/sgv.json?count="+String(GMI_COUNT_30D),m30);
    bool ok90=streamMeanSGV(nsUrl()+"/api/v1/entries/sgv.json?count="+String(GMI_COUNT_90D),m90);
    xSemaphoreTake(dataMutex,portMAX_DELAY);
    if(ok30)gmi30=3.31f+0.02392f*m30;
    if(ok90)gmi90=3.31f+0.02392f*m90;
    xSemaphoreGive(dataMutex);
    if(ok30||ok90){
        gmi_ready=true;
        Serial.printf("[GMI] 30d mean=%.1f A1C=%.2f%% | 90d mean=%.1f A1C=%.2f%%\n",
                      m30,gmi30,m90,gmi90);
    }
}
// ================================================================
// Dexcom Share + LibreLinkUp (unofficial APIs) — ported from the P4 panel.
// Both fill the same shared state (glucose_val/delta/trend_arrow/history) as
// fetchNightscout(), so the rest of the UI is source-agnostic.
// ================================================================
String sha256Hex(const String &s){
    unsigned char d[32];
    mbedtls_sha256((const unsigned char*)s.c_str(), s.length(), d, 0);
    static const char *hx="0123456789abcdef"; char o[65];
    for(int i=0;i<32;i++){o[i*2]=hx[d[i]>>4];o[i*2+1]=hx[d[i]&0xf];}
    o[64]=0; return String(o);
}
// generic JSON HTTP (1 optional extra header); body in *out
int httpJson(const char*method,const String&url,const String&body,
             const char*hn,const char*hv,String*out){
    if(WiFi.status()!=WL_CONNECTED)return -1;
    WiFiClientSecure c;c.setInsecure();HTTPClient h;
    if(!h.begin(c,url)){c.stop();return -1;}
    h.setTimeout(15000);
    h.addHeader("Accept","application/json");
    if(strcmp(method,"POST")==0)h.addHeader("Content-Type","application/json");
    if(hn)h.addHeader(hn,hv);
    int code=(strcmp(method,"POST")==0)?h.POST(body):h.GET();
    if(out)*out=(code>0)?h.getString():String("");
    h.end();c.stop();
    return code;
}

// ---------------- Dexcom Share ----------------
static String s_dexSession="";
String dexHost(){return cfg.dexRegion=="ous"?"shareous1.dexcom.com":"share2.dexcom.com";}
bool dexcomLogin(){
    s_dexSession="";
    String url="https://"+dexHost()+"/ShareWebServices/Services/General/AuthenticatePublisherAccount";
    String body="{\"accountName\":\""+cfg.dexUser+"\",\"password\":\""+cfg.dexPass+"\",\"applicationId\":\""+String(DEX_APPID)+"\"}";
    String resp;
    if(httpJson("POST",url,body,nullptr,nullptr,&resp)!=200)return false;
    JsonDocument d(&g_psram);
    if(deserializeJson(d,resp)!=DeserializationError::Ok)return false;
    String acct=d.as<String>();
    if(acct.length()==0||acct=="null")return false;
    url="https://"+dexHost()+"/ShareWebServices/Services/General/LoginPublisherAccountById";
    body="{\"accountId\":\""+acct+"\",\"password\":\""+cfg.dexPass+"\",\"applicationId\":\""+String(DEX_APPID)+"\"}";
    if(httpJson("POST",url,body,nullptr,nullptr,&resp)!=200)return false;
    JsonDocument d2(&g_psram);
    if(deserializeJson(d2,resp)!=DeserializationError::Ok)return false;
    s_dexSession=d2.as<String>();
    if(s_dexSession=="null")s_dexSession="";
    Serial.println(s_dexSession.length()?"[Dexcom] login ok":"[Dexcom] login FAILED");
    return s_dexSession.length()>0;
}
bool fetchDexcom(){
    if(WiFi.status()!=WL_CONNECTED)return false;
    if(s_dexSession.length()==0&&!dexcomLogin())return false;
    String resp;int code=0;
    for(int attempt=0;attempt<2;attempt++){
        String url="https://"+dexHost()+"/ShareWebServices/Services/Publisher/ReadPublisherLatestGlucoseValues?sessionId="
                   +s_dexSession+"&minutes=180&maxCount=36";
        code=httpJson("POST",url,"",nullptr,nullptr,&resp);
        if(code==200)break;
        s_dexSession="";if(!dexcomLogin())return false;   // session expired -> relogin
    }
    if(code!=200)return false;
    JsonDocument doc(&g_psram);
    if(deserializeJson(doc,resp)!=DeserializationError::Ok||!doc.is<JsonArray>())return false;
    JsonArray arr=doc.as<JsonArray>();int n=arr.size();if(n==0)return false;
    int v=arr[0]["Value"].as<int>();
    String tr=trendToArrow(arr[0]["Trend"].as<String>());
    int d=(n>=2)?v-arr[1]["Value"].as<int>():0;
    int hn=n>SPARK_POINTS?SPARK_POINTS:n;
    xSemaphoreTake(dataMutex,portMAX_DELAY);
    glucose_val=v;trend_arrow=tr;glucose_delta=d;glucoseHistoryCount=hn;
    for(int i=0;i<hn;i++)glucoseHistory[i]=arr[i]["Value"].as<int>();   // arr[0]=newest
    xSemaphoreGive(dataMutex);
    ns_data_ready=true;
    return true;
}

// ---------------- LibreLinkUp ----------------
static String s_libToken="", s_libAcct="", s_libPatient="";
String libreBase(){return "https://api-"+String(cfg.libRegion.length()?cfg.libRegion:"us")+".libreview.io";}
int libreReq(const char*method,const String&url,const String&body,String*out){
    if(WiFi.status()!=WL_CONNECTED)return -1;
    WiFiClientSecure c;c.setInsecure();HTTPClient h;
    if(!h.begin(c,url)){c.stop();return -1;}
    h.setTimeout(15000);
    h.addHeader("product","llu.android");
    h.addHeader("version","4.12.0");
    h.addHeader("Accept","application/json");
    if(strcmp(method,"POST")==0)h.addHeader("Content-Type","application/json");
    if(s_libToken.length())h.addHeader("Authorization","Bearer "+s_libToken);
    if(s_libAcct.length())h.addHeader("Account-Id",s_libAcct);
    int code=(strcmp(method,"POST")==0)?h.POST(body):h.GET();
    if(out)*out=(code>0)?h.getString():String("");
    h.end();c.stop();
    return code;
}
const char* libreTrendStr(int t){
    switch(t){case 1:return"SingleDown";case 2:return"FortyFiveDown";
              case 4:return"FortyFiveUp";case 5:return"SingleUp";default:return"Flat";}
}
bool libreLogin(){
    s_libToken="";s_libAcct="";s_libPatient="";
    String body="{\"email\":\""+cfg.libUser+"\",\"password\":\""+cfg.libPass+"\"}";
    for(int attempt=0;attempt<2;attempt++){
        String resp;
        if(libreReq("POST",libreBase()+"/llu/auth/login",body,&resp)!=200)return false;
        JsonDocument doc(&g_psram);
        if(deserializeJson(doc,resp)!=DeserializationError::Ok)return false;
        JsonObject data=doc["data"];
        if(data.isNull())return false;
        if(data["redirect"].as<bool>()){            // regional redirect -> retry there
            const char*r=data["region"];
            if(r){cfg.libRegion=String(r);Serial.print("[Libre] redirect -> ");Serial.println(cfg.libRegion);}
            continue;
        }
        const char*tok=data["authTicket"]["token"];
        const char*uid=data["user"]["id"];
        if(tok)s_libToken=String(tok);
        if(uid)s_libAcct=sha256Hex(String(uid));    // Account-Id header
        Serial.println(s_libToken.length()?"[Libre] login ok":"[Libre] login FAILED");
        return s_libToken.length()>0;
    }
    return false;
}
bool fetchLibre(){
    if(WiFi.status()!=WL_CONNECTED)return false;
    if(s_libToken.length()==0&&!libreLogin())return false;
    String resp;
    if(s_libPatient.length()==0){                   // discover patientId once
        if(libreReq("GET",libreBase()+"/llu/connections","",&resp)!=200){
            if(!libreLogin())return false;
            if(libreReq("GET",libreBase()+"/llu/connections","",&resp)!=200)return false;
        }
        JsonDocument doc(&g_psram);
        if(deserializeJson(doc,resp)!=DeserializationError::Ok)return false;
        JsonArray data=doc["data"];
        if(data.size()>0){const char*pid=data[0]["patientId"];if(pid)s_libPatient=String(pid);}
        if(s_libPatient.length()==0)return false;
    }
    if(libreReq("GET",libreBase()+"/llu/connections/"+s_libPatient+"/graph","",&resp)!=200){
        s_libToken="";return false;                 // force re-login next poll
    }
    JsonDocument doc(&g_psram);
    if(deserializeJson(doc,resp)!=DeserializationError::Ok)return false;
    JsonObject gm=doc["data"]["connection"]["glucoseMeasurement"];
    if(gm.isNull())return false;
    int v=gm["ValueInMgPerDl"].as<int>(); if(v==0)v=gm["Value"].as<int>();
    int trend=gm["TrendArrow"]|3;
    JsonArray gd=doc["data"]["graphData"];           // oldest -> newest
    int gn=gd.size();int hn=gn>SPARK_POINTS?SPARK_POINTS:gn;
    xSemaphoreTake(dataMutex,portMAX_DELAY);
    glucose_val=v;trend_arrow=trendToArrow(libreTrendStr(trend));glucoseHistoryCount=hn;
    for(int i=0;i<hn;i++){JsonObject pt=gd[gn-1-i];int gv=pt["ValueInMgPerDl"].as<int>();if(gv==0)gv=pt["Value"].as<int>();glucoseHistory[i]=gv;}
    glucose_delta=(hn>=2)?glucose_val-glucoseHistory[1]:0;
    xSemaphoreGive(dataMutex);
    ns_data_ready=true;
    return true;
}
// dispatcher
void fetchGlucose(){
    if(cfg.cgmSource==1)fetchDexcom();
    else if(cfg.cgmSource==2)fetchLibre();
    else fetchNightscout();
}
const char* cgmSourceName(){return cfg.cgmSource==1?"Dexcom":cfg.cgmSource==2?"Libre":"Nightscout";}

// ================================================================
// Home Assistant via MQTT — auto-discovery (the panel is HA's glucose source).
// All MQTT/OTA calls run on the Core-0 fetch task; the web handler only flips a
// flag (PubSubClient / httpUpdate are not thread-safe). blank host = standalone.
// ================================================================
static WiFiClient    mqttNet;
static PubSubClient  mqtt(mqttNet);
static bool          mqttDiscoverySent=false;
static volatile bool mqttReconfig=false;
static unsigned long mqttLastTry=0;

String mqttNodeId(){ String m=WiFi.macAddress(); m.replace(":",""); m.toLowerCase(); return "glucoscout_"+m; }
String mqttHostEff(){
#ifdef MQTT_HOST
    return cfg.mqttHost.length()?cfg.mqttHost:String(MQTT_HOST);
#else
    return cfg.mqttHost;
#endif
}
String mqttUserEff(){
#ifdef MQTT_USER
    return cfg.mqttUser.length()?cfg.mqttUser:String(MQTT_USER);
#else
    return cfg.mqttUser;
#endif
}
String mqttPassEff(){
#ifdef MQTT_PASS
    return cfg.mqttPass.length()?cfg.mqttPass:String(MQTT_PASS);
#else
    return cfg.mqttPass;
#endif
}

void mqttPublishDiscovery(){
    String node=mqttNodeId();
    String stateT="glucoscout/"+node+"/state";
    String availT="glucoscout/"+node+"/status";
    String dev="{\"identifiers\":[\""+node+"\"],\"name\":\"glucoscout panel\",\"mf\":\"glucoscout\",\"mdl\":\"CGM panel\"}";
    auto sensor=[&](const char*key,const char*name,const char*unit,const char*tmpl,const char*icon){
        String t="homeassistant/sensor/"+node+"/"+key+"/config";
        String p="{\"name\":\""+String(name)+"\",\"uniq_id\":\""+node+"_"+key+"\",";
        p+="\"stat_t\":\""+stateT+"\",\"avty_t\":\""+availT+"\",";
        p+="\"val_tpl\":\""+String(tmpl)+"\",";
        if(unit[0])p+="\"unit_of_meas\":\""+String(unit)+"\",";
        if(icon[0])p+="\"ic\":\""+String(icon)+"\",";
        p+="\"dev\":"+dev+"}";
        mqtt.publish(t.c_str(),p.c_str(),true);   // retained
    };
    sensor("glucose","Glucose","mg/dL","{{ value_json.glucose }}","mdi:diabetes");
    sensor("trend","Glucose Trend","","{{ value_json.trend }}","mdi:trending-up");
    sensor("delta","Glucose Delta","mg/dL","{{ value_json.delta }}","mdi:delta");
    sensor("gmi","GMI (est-A1C)","%","{{ value_json.gmi }}","mdi:water-percent");
    mqttDiscoverySent=true;
    Serial.println("[MQTT] HA discovery published");
}

void mqttPublishState(){
    if(!mqtt.connected())return;
    int gv,gd; String ta; float g30;
    xSemaphoreTake(dataMutex,portMAX_DELAY);
    gv=glucose_val; gd=glucose_delta; ta=trend_arrow; g30=gmi30;
    xSemaphoreGive(dataMutex);
    char buf[160];
    snprintf(buf,sizeof(buf),"{\"glucose\":%d,\"trend\":\"%s\",\"delta\":%d,\"gmi\":%.2f}",gv,ta.c_str(),gd,g30);
    String t="glucoscout/"+mqttNodeId()+"/state";
    mqtt.publish(t.c_str(),buf,true);   // retained
}

void mqttEnsure(){            // every ~1s from fetchTask (Core 0): keepalive + reconnect
    static bool inited=false;
    if(mqttReconfig){mqttReconfig=false;if(mqtt.connected())mqtt.disconnect();mqttDiscoverySent=false;}
    if(mqttHostEff().length()==0)return;                    // standalone
    if(mqtt.connected()){mqtt.loop();return;}
    unsigned long now=millis();
    if(inited && (int32_t)(now-mqttLastTry)<5000)return;    // throttle reconnect
    mqttLastTry=now;
    if(!inited){mqtt.setBufferSize(512);inited=true;}       // HA discovery configs ~400B
    int port=cfg.mqttPort>0?cfg.mqttPort:1883;
    mqtt.setServer(mqttHostEff().c_str(),port);
    String node=mqttNodeId(), availT="glucoscout/"+node+"/status";
    String u=mqttUserEff(),pw=mqttPassEff();
    bool ok=mqtt.connect(node.c_str(), u.length()?u.c_str():nullptr, pw.length()?pw.c_str():nullptr,
                         availT.c_str(),0,true,"offline");
    if(ok){
        Serial.printf("[MQTT] connected -> %s:%d\n",mqttHostEff().c_str(),port);
        mqtt.publish(availT.c_str(),"online",true);
        mqttPublishDiscovery();
        mqttPublishState();
    } else Serial.printf("[MQTT] connect failed rc=%d\n",mqtt.state());
}

// ---- internet OTA: pull updates over HTTPS from the GitHub per-board manifest ----
#ifndef OTA_MANIFEST_URL
#define OTA_MANIFEST_URL "https://raw.githubusercontent.com/glucoscout/esp32-cgm/main/boards/esp32-s3/guition-3.5in/manifest.json"
#endif
#define OTA_CHECK_MS (24UL*60UL*60000UL)   // auto-check daily
static volatile bool otaRequested=false;
static String otaStatus="idle";

String otaFetchManifest(String& binUrl){
    WiFiClientSecure c; c.setInsecure();
    HTTPClient h; h.setConnectTimeout(8000); h.setTimeout(8000);
    if(!h.begin(c, OTA_MANIFEST_URL)) return "";
    String ver="";
    if(h.GET()==200){
        JsonDocument d(&g_psram);
        if(deserializeJson(d,h.getStream())==DeserializationError::Ok){
            ver=String((const char*)(d["version"]|""));
            binUrl=String((const char*)(d["url"]|""));
        }
    }
    h.end(); return ver;
}
void otaRunUpdate(){          // Core 0 (fetchTask) ONLY — blocking; reboots on success
    String binUrl, latest=otaFetchManifest(binUrl);
    if(latest.length()==0){otaStatus="check failed"; Serial.println("[OTA] manifest fetch failed"); return;}
    if(latest==String(FW_VERSION)){otaStatus="up to date ("+latest+")"; Serial.printf("[OTA] up to date %s\n",latest.c_str()); return;}
    if(binUrl.length()==0){otaStatus="newer version, no url"; return;}
    otaStatus="updating "+String(FW_VERSION)+" -> "+latest;
    Serial.printf("[OTA] %s -> %s : %s\n",FW_VERSION,latest.c_str(),binUrl.c_str());
    WiFiClientSecure uc; uc.setInsecure();
    httpUpdate.rebootOnUpdate(true);
    t_httpUpdate_return r=httpUpdate.update(uc, binUrl);
    if(r==HTTP_UPDATE_FAILED) otaStatus="failed: "+httpUpdate.getLastErrorString();
    else if(r==HTTP_UPDATE_NO_UPDATES) otaStatus="no update";
    // HTTP_UPDATE_OK reboots automatically
}
void handleOtaCheck(){ otaRequested=true; configServer.send(200,"text/plain","Checking for updates..."); }
void handleOtaStatus(){ configServer.send(200,"text/plain", String(FW_VERSION)+" | "+otaStatus); }

void fetchTask(void*p){
    while(WiFi.status()!=WL_CONNECTED)vTaskDelay(pdMS_TO_TICKS(500));
    unsigned long nNS=0,nWX=0,nGMI=0,nOTA=0,taskStart=millis();bool gmiInit=false;
    Serial.printf("[CGM] source=%s\n",cgmSourceName());
    for(;;){
        unsigned long now=millis();checkWiFi();
        mqttEnsure();
        if(otaRequested){otaRequested=false;otaRunUpdate();}
        if(now-nNS>=NS_UPDATE_MS){nNS=now;fetchGlucose();mqttPublishState();}
        if(now-nOTA>=OTA_CHECK_MS){nOTA=now;otaRunUpdate();}
        if(now-nWX>=WEATHER_UPDATE_MS){nWX=now;fetchWeather();}
        // GMI (est-A1C) only for Nightscout: first ~15s after boot, then hourly
        if(cfg.cgmSource==0){
            if(!gmiInit){if(now-taskStart>=15000){gmiInit=true;nGMI=now;fetchGMI();}}
            else if(now-nGMI>=GMI_UPDATE_MS){nGMI=now;fetchGMI();}
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ================================================================
// Sparkline (last 3hr of glucose, drawn on lv_canvas)
// ================================================================
void drawSparkline(){
    if(!spark_canvas||!spark_buf)return;
    lv_canvas_fill_bg(spark_canvas,lv_color_hex(0x0A1622),LV_OPA_COVER);
    xSemaphoreTake(dataMutex,portMAX_DELAY);
    int n=glucoseHistoryCount;
    int hist[SPARK_POINTS];
    for(int i=0;i<n;i++)hist[i]=glucoseHistory[i];
    xSemaphoreGive(dataMutex);
    if(n<2)return;
    int lo=hist[0],hi=hist[0];
    for(int i=1;i<n;i++){if(hist[i]<lo)lo=hist[i];if(hist[i]>hi)hi=hist[i];}
    int range=hi-lo;
    if(range<40){int m=(hi+lo)/2;lo=m-20;hi=m+20;range=40;}
    // Reference dashed lines at critical thresholds
    auto yfor=[&](int v)->int{
        int y=SPARK_H-1-((v-lo)*(SPARK_H-3))/range-1;
        if(y<0)y=0;if(y>=SPARK_H)y=SPARK_H-1;return y;
    };
    if(cfg.critLow>=lo&&cfg.critLow<=hi){
        int y=yfor(cfg.critLow);
        for(int x=0;x<SPARK_W;x+=4)lv_canvas_set_px(spark_canvas,x,y,lv_color_hex(0x553322));
    }
    if(cfg.critHigh>=lo&&cfg.critHigh<=hi){
        int y=yfor(cfg.critHigh);
        for(int x=0;x<SPARK_W;x+=4)lv_canvas_set_px(spark_canvas,x,y,lv_color_hex(0x553322));
    }
    // Polyline: hist[0]=newest at right, hist[n-1]=oldest at left
    lv_draw_line_dsc_t ld;lv_draw_line_dsc_init(&ld);
    ld.color=lv_color_hex(0x00FF88);ld.width=2;ld.round_start=1;ld.round_end=1;
    lv_point_t pts[2];
    for(int i=0;i<n-1;i++){
        int x1=SPARK_W-1-(i*(SPARK_W-1))/(n-1);
        int x2=SPARK_W-1-((i+1)*(SPARK_W-1))/(n-1);
        pts[0].x=x2;pts[0].y=yfor(hist[i+1]);
        pts[1].x=x1;pts[1].y=yfor(hist[i]);
        ld.color=glucoseColor(hist[i]);
        lv_canvas_draw_line(spark_canvas,pts,2,&ld);
    }
    // Dot at most-recent value
    int yn=yfor(hist[0]);
    lv_draw_rect_dsc_t rd;lv_draw_rect_dsc_init(&rd);
    rd.bg_color=glucoseColor(hist[0]);rd.radius=3;rd.border_width=0;
    lv_canvas_draw_rect(spark_canvas,SPARK_W-5,yn-2,5,5,&rd);
}

// ================================================================
// Touch settings menu
// ================================================================
struct SettingDef { const char *label; int *value; int lo; int hi; int step; const char *unit; };
static SettingDef gSettings[6];
static lv_obj_t  *gSettingVal[6];
static int        gSettingCount = 0;

static void updateSettingLabel(int idx){
    if(idx<0||idx>=gSettingCount)return;
    char b[32];
    SettingDef &s=gSettings[idx];
    int v=*(s.value);
    if(strcmp(s.unit,"sec")==0)v/=1000;
    snprintf(b,sizeof(b),"%d%s",v,s.unit);
    lv_label_set_text(gSettingVal[idx],b);
}

static void settingBtn_cb(lv_event_t *e){
    int packed=(int)(intptr_t)lv_event_get_user_data(e);
    int idx=packed>>1;int dir=(packed&1)?1:-1;
    SettingDef &s=gSettings[idx];
    int v=*(s.value);
    if(strcmp(s.unit,"sec")==0){
        int sec=v/1000;sec+=dir*s.step;
        if(sec<s.lo)sec=s.lo;if(sec>s.hi)sec=s.hi;
        *(s.value)=sec*1000;
    } else {
        v+=dir*s.step;
        if(v<s.lo)v=s.lo;if(v>s.hi)v=s.hi;
        *(s.value)=v;
    }
    updateSettingLabel(idx);
}

static void settingsClose_cb(lv_event_t *e){
    saveConfig();
    if(settings_modal){lv_obj_del(settings_modal);settings_modal=nullptr;}
    inSettings=false;
    struct tm ti;
    if(getLocalTime(&ti)){
        bool n=(ti.tm_hour>=cfg.nightStart&&ti.tm_hour<cfg.nightEnd);
        bsp_display_brightness_set(n?cfg.nightBright:cfg.dayBright);
    }
}

static void addSettingRow(lv_obj_t *parent,int idx,const char *label,int *value,int lo,int hi,int step,const char *unit){
    gSettings[idx]={label,value,lo,hi,step,unit};gSettingCount=idx+1;
    lv_obj_t *row=lv_obj_create(parent);
    lv_obj_set_size(row,460,36);
    lv_obj_set_style_bg_opa(row,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(row,0,0);
    lv_obj_set_style_pad_all(row,2,0);
    lv_obj_clear_flag(row,LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl=lv_label_create(row);
    lv_label_set_text(lbl,label);
    lv_obj_set_style_text_color(lbl,lv_color_hex(0xFFFFFF),0);
    lv_obj_set_style_text_font(lbl,&lv_font_montserrat_14,0);
    lv_obj_align(lbl,LV_ALIGN_LEFT_MID,4,0);

    lv_obj_t *bm=lv_btn_create(row);
    lv_obj_set_size(bm,44,30);
    lv_obj_align(bm,LV_ALIGN_RIGHT_MID,-110,0);
    lv_obj_set_style_bg_color(bm,lv_color_hex(0x445566),0);
    lv_obj_set_style_radius(bm,6,0);
    lv_obj_add_event_cb(bm,settingBtn_cb,LV_EVENT_CLICKED,(void*)(intptr_t)(idx<<1));
    lv_obj_t *lm=lv_label_create(bm);
    lv_label_set_text(lm,"-");lv_obj_set_style_text_font(lm,&lv_font_montserrat_20,0);lv_obj_center(lm);

    lv_obj_t *vl=lv_label_create(row);
    lv_obj_set_style_text_color(vl,lv_color_hex(0xFFDD00),0);
    lv_obj_set_style_text_font(vl,&lv_font_montserrat_16,0);
    lv_obj_align(vl,LV_ALIGN_RIGHT_MID,-58,0);
    lv_obj_set_width(vl,46);
    lv_obj_set_style_text_align(vl,LV_TEXT_ALIGN_CENTER,0);
    gSettingVal[idx]=vl;
    updateSettingLabel(idx);

    lv_obj_t *bp=lv_btn_create(row);
    lv_obj_set_size(bp,44,30);
    lv_obj_align(bp,LV_ALIGN_RIGHT_MID,-4,0);
    lv_obj_set_style_bg_color(bp,lv_color_hex(0x00AA66),0);
    lv_obj_set_style_radius(bp,6,0);
    lv_obj_add_event_cb(bp,settingBtn_cb,LV_EVENT_CLICKED,(void*)(intptr_t)((idx<<1)|1));
    lv_obj_t *lp=lv_label_create(bp);
    lv_label_set_text(lp,"+");lv_obj_set_style_text_font(lp,&lv_font_montserrat_20,0);lv_obj_center(lp);
}

void showSettingsMenu(){
    if(inSettings||settings_modal)return;
    inSettings=true;
    bsp_display_lock(100);
    settings_modal=lv_obj_create(lv_scr_act());
    lv_obj_set_size(settings_modal,480,320);lv_obj_set_pos(settings_modal,0,0);
    lv_obj_set_style_bg_color(settings_modal,lv_color_hex(0x060A14),0);
    lv_obj_set_style_bg_opa(settings_modal,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(settings_modal,0,0);
    lv_obj_set_style_pad_all(settings_modal,5,0);
    lv_obj_clear_flag(settings_modal,LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t=lv_label_create(settings_modal);
    lv_label_set_text(t,"SETTINGS");
    lv_obj_set_style_text_color(t,lv_color_hex(0xE74C3C),0);
    lv_obj_set_style_text_font(t,&lv_font_montserrat_18,0);
    lv_obj_align(t,LV_ALIGN_TOP_MID,0,2);

    int listY=28;

    lv_obj_t *list=lv_obj_create(settings_modal);
    lv_obj_set_size(list,470,235);
    lv_obj_set_pos(list,0,listY);
    lv_obj_set_style_bg_opa(list,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(list,0,0);
    lv_obj_set_style_pad_all(list,2,0);
    lv_obj_set_flex_flow(list,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list,2,0);

    gSettingCount=0;
    addSettingRow(list,0,"Day Brightness",&cfg.dayBright,10,100,5,"%");
    addSettingRow(list,1,"Night Brightness",&cfg.nightBright,5,100,5,"%");
    addSettingRow(list,2,"Critical Low",&cfg.critLow,40,100,5,"");
    addSettingRow(list,3,"Critical High",&cfg.critHigh,150,400,10,"");

    lv_obj_t *btn=lv_btn_create(settings_modal);
    lv_obj_set_size(btn,460,38);
    lv_obj_align(btn,LV_ALIGN_BOTTOM_MID,0,-3);
    lv_obj_set_style_bg_color(btn,lv_color_hex(0xE74C3C),0);
    lv_obj_set_style_radius(btn,8,0);
    lv_obj_add_event_cb(btn,settingsClose_cb,LV_EVENT_CLICKED,NULL);
    lv_obj_t *bl=lv_label_create(btn);
    lv_label_set_text(bl,"Save & Close");
    lv_obj_set_style_text_color(bl,lv_color_hex(0xFFFFFF),0);
    lv_obj_set_style_text_font(bl,&lv_font_montserrat_18,0);
    lv_obj_center(bl);

    bsp_display_unlock();
}

static void screenLongPress_cb(lv_event_t *e){
    if(!inSettings)showSettingsMenu();
}

// ================================================================
// LVGL Dashboard UI
// ================================================================
void createDashboardUI(){
    lv_obj_t*scr=lv_scr_act();
    lv_obj_set_style_bg_color(scr,lv_color_hex(0x060A14),0);
    lv_obj_set_style_bg_opa(scr,LV_OPA_COVER,0);

    lv_obj_t*tb=lv_obj_create(scr);
    lv_obj_set_size(tb,480,52);lv_obj_set_pos(tb,0,0);
    lv_obj_set_style_bg_color(tb,lv_color_hex(0x0D1B2A),0);
    lv_obj_set_style_border_width(tb,0,0);lv_obj_set_style_radius(tb,0,0);
    lv_obj_clear_flag(tb,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tb,LV_OBJ_FLAG_EVENT_BUBBLE);
    lbl_date=lv_label_create(tb);lv_label_set_text(lbl_date,"Loading...");
    lv_obj_set_style_text_color(lbl_date,lv_color_hex(0x8899AA),0);
    lv_obj_set_style_text_font(lbl_date,&lv_font_montserrat_16,0);
    lv_obj_align(lbl_date,LV_ALIGN_LEFT_MID,12,0);
    lbl_time=lv_label_create(tb);lv_label_set_text(lbl_time,"--:-- --");
    lv_obj_set_style_text_color(lbl_time,lv_color_hex(0xFFFFFF),0);
    lv_obj_set_style_text_font(lbl_time,&lv_font_montserrat_22,0);
    lv_obj_align(lbl_time,LV_ALIGN_RIGHT_MID,-12,0);
    lbl_wifi=lv_label_create(tb);lv_label_set_text(lbl_wifi,"--- dBm");
    lv_obj_set_style_text_color(lbl_wifi,lv_color_hex(0xDD00FF),0);
    lv_obj_set_style_text_font(lbl_wifi,&lv_font_montserrat_12,0);
    lv_obj_align(lbl_wifi,LV_ALIGN_CENTER,0,0);

    lv_obj_t*gc=lv_obj_create(scr);lv_obj_set_size(gc,460,150);lv_obj_set_pos(gc,10,62);
    lv_obj_set_style_bg_color(gc,lv_color_hex(0x0D1B2A),0);
    lv_obj_set_style_border_color(gc,lv_color_hex(0x1A3A5C),0);
    lv_obj_set_style_border_width(gc,2,0);lv_obj_set_style_radius(gc,16,0);
    lv_obj_clear_flag(gc,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(gc,LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_pad_all(gc,0,0);
    lv_obj_t*gt=lv_label_create(gc);lv_label_set_text(gt,"BLOOD GLUCOSE  mg/dL  (3h trend)");
    lv_obj_set_style_text_color(gt,lv_color_hex(0x556677),0);
    lv_obj_set_style_text_font(gt,&lv_font_montserrat_12,0);lv_obj_align(gt,LV_ALIGN_TOP_LEFT,12,6);
    lbl_glucose=lv_label_create(gc);lv_label_set_text(lbl_glucose,"---");
    lv_obj_set_style_text_color(lbl_glucose,lv_color_hex(0x00FF88),0);
    lv_obj_set_style_text_font(lbl_glucose,&lv_font_montserrat_48,0);
    lv_obj_align(lbl_glucose,LV_ALIGN_TOP_LEFT,12,18);
    lbl_trend=lv_label_create(gc);lv_label_set_text(lbl_trend,"->  +0");
    lv_obj_set_style_text_color(lbl_trend,lv_color_hex(0xFFDD00),0);
    lv_obj_set_style_text_font(lbl_trend,&lv_font_montserrat_28,0);
    lv_obj_align(lbl_trend,LV_ALIGN_TOP_RIGHT,-12,30);
    lbl_gmi=lv_label_create(gc);lv_label_set_text(lbl_gmi,"GMI --");
    lv_obj_set_style_text_color(lbl_gmi,lv_color_hex(0x66AAFF),0);
    lv_obj_set_style_text_font(lbl_gmi,&lv_font_montserrat_14,0);
    lv_obj_align(lbl_gmi,LV_ALIGN_TOP_RIGHT,-12,4);
    // Sparkline canvas at bottom of glucose card
    if(!spark_buf){
        spark_buf=(lv_color_t*)heap_caps_malloc(SPARK_W*SPARK_H*sizeof(lv_color_t),MALLOC_CAP_SPIRAM);
    }
    if(spark_buf){
        spark_canvas=lv_canvas_create(gc);
        lv_canvas_set_buffer(spark_canvas,spark_buf,SPARK_W,SPARK_H,LV_IMG_CF_TRUE_COLOR);
        lv_obj_set_pos(spark_canvas,12,90);
        lv_canvas_fill_bg(spark_canvas,lv_color_hex(0x0A1622),LV_OPA_COVER);
    }

    lv_obj_t*wc=lv_obj_create(scr);lv_obj_set_size(wc,222,95);lv_obj_set_pos(wc,10,222);
    lv_obj_set_style_bg_color(wc,lv_color_hex(0x0A2540),0);
    lv_obj_set_style_border_color(wc,lv_color_hex(0x1A3A5C),0);
    lv_obj_set_style_border_width(wc,2,0);lv_obj_set_style_radius(wc,16,0);
    lv_obj_clear_flag(wc,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wc,LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t*wt=lv_label_create(wc);
    String wtTxt="WEATHER - "+cfg.city;wtTxt.toUpperCase();
    lv_label_set_text(wt,wtTxt.c_str());
    lv_obj_set_style_text_color(wt,lv_color_hex(0x556677),0);
    lv_obj_set_style_text_font(wt,&lv_font_montserrat_10,0);lv_obj_align(wt,LV_ALIGN_TOP_LEFT,10,8);
    lbl_weather=lv_label_create(wc);lv_label_set_text(lbl_weather,"--F");
    lv_obj_set_style_text_color(lbl_weather,lv_color_hex(0x00C8FF),0);
    lv_obj_set_style_text_font(lbl_weather,&lv_font_montserrat_22,0);
    lv_obj_align(lbl_weather,LV_ALIGN_LEFT_MID,10,8);

    lv_obj_t*fk=lv_obj_create(scr);lv_obj_set_size(fk,238,95);lv_obj_set_pos(fk,242,222);
    lv_obj_set_style_bg_color(fk,lv_color_hex(0x0A2540),0);
    lv_obj_set_style_border_color(fk,lv_color_hex(0x1A3A5C),0);
    lv_obj_set_style_border_width(fk,2,0);lv_obj_set_style_radius(fk,16,0);
    lv_obj_clear_flag(fk,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(fk,LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_pad_all(fk,0,0);
    lv_obj_t*ft=lv_label_create(fk);lv_label_set_text(ft,"4-DAY FORECAST");
    lv_obj_set_style_text_color(ft,lv_color_hex(0x556677),0);
    lv_obj_set_style_text_font(ft,&lv_font_montserrat_10,0);lv_obj_align(ft,LV_ALIGN_TOP_LEFT,10,6);
    for(int i=0;i<4;i++){
        lbl_fc[i]=lv_label_create(fk);
        lv_obj_set_width(lbl_fc[i],54);
        lv_obj_set_style_text_align(lbl_fc[i],LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_text_color(lbl_fc[i],lv_color_hex(0xC8D6E5),0);
        lv_obj_set_style_text_font(lbl_fc[i],&lv_font_montserrat_12,0);
        lv_obj_set_pos(lbl_fc[i],6+i*58,26);
        lv_label_set_text(lbl_fc[i],"--\n--\n--");
    }

    lv_obj_t*sb=lv_obj_create(scr);lv_obj_set_size(sb,480,28);lv_obj_set_pos(sb,0,327);
    lv_obj_set_style_bg_color(sb,lv_color_hex(0x0D1B2A),0);
    lv_obj_set_style_border_width(sb,0,0);lv_obj_set_style_radius(sb,0,0);
    lv_obj_clear_flag(sb,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sb,LV_OBJ_FLAG_EVENT_BUBBLE);
    lbl_status=lv_label_create(sb);lv_label_set_text(lbl_status,"Connecting...");
    lv_obj_set_style_text_color(lbl_status,lv_color_hex(0x445566),0);
    lv_obj_set_style_text_font(lbl_status,&lv_font_montserrat_12,0);
    lv_obj_align(lbl_status,LV_ALIGN_CENTER,0,0);
    lv_obj_t*ip=lv_label_create(sb);
    lv_label_set_text(ip,WiFi.localIP().toString().c_str());
    lv_obj_set_style_text_color(ip,lv_color_hex(0x334455),0);
    lv_obj_set_style_text_font(ip,&lv_font_montserrat_10,0);
    lv_obj_align(ip,LV_ALIGN_RIGHT_MID,-8,0);
}

// ================================================================
// UI updates
// ================================================================
void updateDashboardUI(){
    xSemaphoreTake(dataMutex,portMAX_DELAY);
    int gv=glucose_val,gd=glucose_delta;
    String ta=trend_arrow,ws=weather_str;
    float g30=gmi30,g90=gmi90;
    FcDay fc[4]; for(int i=0;i<4;i++)fc[i]=forecast[i];
    xSemaphoreGive(dataMutex);
    if(lbl_gmi){
        if(g30>0||g90>0){
            char gb[40];snprintf(gb,sizeof(gb),"GMI %.1f/%.1f%%",g30,g90);
            lv_label_set_text(lbl_gmi,gb);
        } else lv_label_set_text(lbl_gmi,"GMI --");
    }
    for(int i=0;i<4;i++){
        if(!lbl_fc[i])continue;
        if(fc[i].valid){
            char b[24];snprintf(b,sizeof(b),"%s\n%s\n%d/%d",fc[i].dow,wxShort(fc[i].code),fc[i].hi,fc[i].lo);
            lv_label_set_text(lbl_fc[i],b);
        } else lv_label_set_text(lbl_fc[i],"--\n--\n--");
    }
    lv_label_set_text(lbl_glucose,gv>0?String(gv).c_str():"---");
    lv_obj_set_style_text_color(lbl_glucose,glucoseColor(gv),0);
    lv_label_set_text(lbl_trend,(ta+(gd>=0?" +"+String(gd):" "+String(gd))).c_str());
    lv_label_set_text(lbl_weather,ws.c_str());
    lv_label_set_text(lbl_wifi,(String(WiFi.RSSI())+" dBm").c_str());
    drawSparkline();
    struct tm ti;
    if(getLocalTime(&ti)){
        char b[40];strftime(b,sizeof(b),"Last updated %I:%M %p",&ti);
        lv_label_set_text(lbl_status,b);
    }
}
// ================================================================
// Dashboard entry
// ================================================================
void enterDashboard(){
    spark_canvas=nullptr;
    bsp_display_lock(100);lv_obj_clean(lv_scr_act());
    createDashboardUI();updateDashboardUI();bsp_display_unlock();
}
void applyBrightness(int h){
    bool n=(h>=cfg.nightStart&&h<cfg.nightEnd);
    bsp_display_brightness_set(n?cfg.nightBright:cfg.dayBright);
}

// ================================================================
// Config page HTML
// ================================================================
// Streamed in chunks (chunked transfer) so the whole ~7KB page is never held
// in internal RAM at once — only the largest single static chunk is transient.
void streamConfigPage(){
    String html; html.reserve(7000);
    /* page buffered, sent at end */
    html += (R"HTML(<!DOCTYPE html>
<html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>CGM Config</title>
<style>
:root{--bg:#f5f5f5;--card:#fff;--accent:#e74c3c;--text:#222;--muted:#666;--border:#ddd;--r:12px}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:var(--bg);color:var(--text);padding:16px}
h1{font-size:1.3rem;color:var(--accent);margin-bottom:4px}
.sub{font-size:.8rem;color:var(--muted);margin-bottom:18px}
nav{display:flex;gap:8px;margin-bottom:18px}
nav a{display:inline-block;padding:8px 14px;border-radius:8px;font-size:.85rem;font-weight:700;text-decoration:none;background:var(--card);color:var(--muted);border:1px solid var(--border)}
nav a.active{background:var(--accent);color:#fff;border-color:var(--accent)}
.card{background:var(--card);border-radius:var(--r);box-shadow:0 1px 4px rgba(0,0,0,.1);padding:16px;margin-bottom:14px}
.card h2{font-size:.8rem;font-weight:700;color:var(--muted);text-transform:uppercase;letter-spacing:.06em;margin-bottom:12px}
.row{display:flex;align-items:center;justify-content:space-between;padding:10px 0;border-bottom:1px solid var(--border);gap:12px}
.row:last-child{border-bottom:none;padding-bottom:0}
label{font-size:.95rem;flex:1}
label small{display:block;font-size:.75rem;color:var(--muted);margin-top:2px}
input[type=number]{width:80px;padding:7px 10px;border:1px solid var(--border);border-radius:8px;font-size:.95rem;text-align:right;background:#fafafa}
input[type=number]:focus{outline:2px solid var(--accent);border-color:var(--accent)}
.unit{font-size:.8rem;color:var(--muted);min-width:32px}
.brow{display:flex;gap:10px;margin-top:6px}
button{border:none;border-radius:10px;padding:13px 20px;font-size:.95rem;font-weight:700;cursor:pointer;flex:1}
.bs{background:var(--accent);color:#fff}.br{background:#eee;color:#333}
.toast{display:none;background:#2ecc71;color:#fff;text-align:center;padding:10px;border-radius:10px;margin-bottom:14px;font-weight:700;font-size:.9rem}
.toast.err{background:var(--accent)}
.ip{font-size:.75rem;color:var(--muted);text-align:center;margin-top:16px}
</style></head><body>
<h1>&#127973; CGM Dashboard</h1>
<p class="sub">Settings saved to device — survive reboot</p>
<nav><a href="/" class="active">Settings</a></nav>
<div class="toast" id="toast"></div>
<form id="frm">
<div class="card"><h2>Brightness</h2>
<div class="row"><label>Day brightness<small>Normal hours</small></label>
<input type="number" name="dayBright" min="10" max="100" value=")HTML");
    html += (String(cfg.dayBright));
    html += (R"HTML("><span class="unit">%</span></div>
<div class="row"><label>Night brightness<small>Dim during night mode</small></label>
<input type="number" name="nightBright" min="5" max="100" value=")HTML");
    html += (String(cfg.nightBright));
    html += (R"HTML("><span class="unit">%</span></div></div>
<div class="card"><h2>Night Mode Hours</h2>
<div class="row"><label>Dim at hour<small>24-hour (1 = 1:00 AM)</small></label>
<input type="number" name="nightStart" min="0" max="23" value=")HTML");
    html += (String(cfg.nightStart));
    html += (R"HTML("><span class="unit">hr</span></div>
<div class="row"><label>Brighten at hour<small>24-hour (7 = 7:00 AM)</small></label>
<input type="number" name="nightEnd" min="0" max="23" value=")HTML");
    html += (String(cfg.nightEnd));
    html += (R"HTML("><span class="unit">hr</span></div></div>
<div class="card"><h2>Display Timing</h2>
<div class="row"><label>Dashboard time<small>How long dashboard shows</small></label>
<input type="number" name="dashboardSec" min="3" max="120" value=")HTML");
    html += (String(cfg.dashboardMs/1000));
    html += (R"HTML("><span class="unit">sec</span></div></div>
<div class="card"><h2>Glucose Alert Thresholds</h2>
<div class="row"><label>Critical LOW<small>Red flash + LOW! overlay</small></label>
<input type="number" name="critLow" min="40" max="100" value=")HTML");
    html += (String(cfg.critLow));
    html += (R"HTML("><span class="unit">mg/dL</span></div>
<div class="row"><label>Critical HIGH<small>Red flash + HIGH! overlay</small></label>
<input type="number" name="critHigh" min="150" max="400" value=")HTML");
    html += (String(cfg.critHigh));
    html += (R"HTML("><span class="unit">mg/dL</span></div></div>
<div class="card"><h2>Glucose Source</h2>
<div class="row"><label>Data source<small>Where glucose readings come from</small></label>
<select name="cgmSource" id="cgmSource" onchange="srcChange()" style="padding:7px 10px;border:1px solid var(--border);border-radius:8px;font-size:.95rem">
<option value="0")HTML");
    if(cfg.cgmSource==0)html += (" selected");
    html += (R"HTML(>Nightscout</option>
<option value="1")HTML");
    if(cfg.cgmSource==1)html += (" selected");
    html += (R"HTML(>Dexcom Share</option>
<option value="2")HTML");
    if(cfg.cgmSource==2)html += (" selected");
    html += (R"HTML(>LibreLinkUp</option>
</select></div>
<div id="ns_fields"><div class="row"><label>Nightscout URL</label><input type="text" name="nsurl" style="width:210px;text-align:left" value=")HTML");
    html += (nsUrl());
    html += (R"HTML("></div><div class="row"><label>Nightscout secret<small>blank = keep</small></label><input type="password" name="nssecret" placeholder="(unchanged)" style="width:140px"></div></div>
<div id="dex_fields" style="display:none">
<div class="row"><label>Dexcom username</label><input type="text" name="dexUser" style="width:160px;text-align:left" value=")HTML");
    html += (cfg.dexUser);
    html += (R"HTML("></div>
<div class="row"><label>Dexcom password<small>blank = keep current</small></label><input type="password" name="dexPass" placeholder="(unchanged)" style="width:140px"></div>
<div class="row"><label>Region</label><select name="dexRegion" style="padding:7px 10px;border:1px solid var(--border);border-radius:8px"><option value="us")HTML");
    if(cfg.dexRegion!="ous")html += (" selected");
    html += (R"HTML(>US</option><option value="ous")HTML");
    if(cfg.dexRegion=="ous")html += (" selected");
    html += (R"HTML(>Outside US</option></select></div>
</div>
<div id="lib_fields" style="display:none">
<div class="row"><label>Libre email</label><input type="text" name="libUser" style="width:160px;text-align:left" value=")HTML");
    html += (cfg.libUser);
    html += (R"HTML("></div>
<div class="row"><label>Libre password<small>blank = keep current</small></label><input type="password" name="libPass" placeholder="(unchanged)" style="width:140px"></div>
<div class="row"><label>Region<small>us / eu / de / fr ...</small></label><input type="text" name="libRegion" maxlength="6" style="width:80px" value=")HTML");
    html += (cfg.libRegion);
    html += (R"HTML("></div>
</div></div>
<div class="card"><h2>Wi-Fi Network</h2>
<div class="row"><label>Wi-Fi network<small>SSID the panel connects to</small></label>
<input type="text" name="wifiSsid" style="width:170px;text-align:left" value=")HTML");
    html += (wifiSsidEff());
    html += (R"HTML("></div>
<div class="row"><label>Wi-Fi password<small>blank = keep current</small></label>
<input type="password" name="wifiPass" placeholder="(unchanged)" style="width:140px"></div></div>
<div class="card"><h2>Weather Location</h2>
<div class="row"><label>City label<small>Shows on dashboard header</small></label>
<input type="text" name="city" maxlength="30" style="width:140px;text-align:left" value=")HTML");
    html += (cfg.city);
    html += (R"HTML("></div>
<div class="row"><label>Latitude<small>Decimal, e.g. 40.7128</small></label>
<input type="text" name="lat" maxlength="12" style="width:100px" value=")HTML");
    html += (cfg.lat);
    html += (R"HTML("></div>
<div class="row"><label>Longitude<small>Decimal, e.g. -74.0060</small></label>
<input type="text" name="lon" maxlength="12" style="width:100px" value=")HTML");
    html += (cfg.lon);
    html += (R"HTML("></div>
<div class="row"><label>Temperature unit<small>F or C</small></label>
<select name="units" style="padding:7px 10px;border:1px solid var(--border);border-radius:8px;font-size:.95rem"><option value="F")HTML");
    if(!cfg.isCelsius)html += (" selected");
    html += (R"HTML(>Fahrenheit</option><option value="C")HTML");
    if(cfg.isCelsius)html += (" selected");
    html += (R"HTML(>Celsius</option></select></div>
<div class="row"><label>Lookup by city name<small>Auto-fills lat/lon</small></label>
<button type="button" class="br" style="padding:7px 14px;font-size:.85rem" onclick="lookupCity()">Find Coords</button></div></div>
<div class="card"><h2>Time Zone</h2>
<div class="row"><label>Region<small>Auto-handles DST</small></label>
<select name="tz" style="padding:7px 10px;border:1px solid var(--border);border-radius:8px;font-size:.95rem;max-width:220px">)HTML");
    auto tzOpt=[&](const char* val,const char* lbl){
        String o="<option value=\"";o+=val;o+="\"";
        if(cfg.tzString==val)o+=" selected";
        o+=">";o+=lbl;o+="</option>";
        html += (o);
    };
    tzOpt("EST5EDT,M3.2.0,M11.1.0","US Eastern");
    tzOpt("CST6CDT,M3.2.0,M11.1.0","US Central");
    tzOpt("MST7MDT,M3.2.0,M11.1.0","US Mountain");
    tzOpt("MST7","US Arizona (no DST)");
    tzOpt("PST8PDT,M3.2.0,M11.1.0","US Pacific");
    tzOpt("AKST9AKDT,M3.2.0,M11.1.0","US Alaska");
    tzOpt("HST10","US Hawaii");
    tzOpt("GMT0BST,M3.5.0/1,M10.5.0","UK / Ireland");
    tzOpt("CET-1CEST,M3.5.0,M10.5.0/3","Central Europe");
    tzOpt("EET-2EEST,M3.5.0/3,M10.5.0/4","Eastern Europe");
    tzOpt("MSK-3","Moscow");
    tzOpt("GST-4","Dubai / Gulf");
    tzOpt("IST-5:30","India");
    tzOpt("ICT-7","Thailand / Vietnam");
    tzOpt("CST-8","China / Singapore");
    tzOpt("JST-9","Japan / Korea");
    tzOpt("AEST-10AEDT,M10.1.0,M4.1.0/3","Australia (Sydney)");
    tzOpt("NZST-12NZDT,M9.5.0,M4.1.0/3","New Zealand");
    tzOpt("UTC0","UTC");
    html += (R"HTML(</select></div></div>
<div class="brow">
<button class="bs" type="button" onclick="doSave()">Save Settings</button>
<button class="br" type="button" onclick="doRestart()">Restart Board</button>
</div></form>
<p class="ip">CGM-Dashboard &#8226; )HTML");
    html += (WiFi.localIP().toString());
    html += (R"HTML(</p>
<script>
var toast=document.getElementById("toast");
function showToast(m,e){toast.textContent=m;toast.className="toast"+(e?" err":"");
  toast.style.display="block";setTimeout(function(){toast.style.display="none";},3000);}
function srcChange(){var s=document.getElementById("cgmSource").value;
  document.getElementById("ns_fields").style.display=(s=="0")?"block":"none";
  document.getElementById("dex_fields").style.display=(s=="1")?"block":"none";
  document.getElementById("lib_fields").style.display=(s=="2")?"block":"none";}
window.addEventListener("load",srcChange);
function doSave(){
  var b=new URLSearchParams(new FormData(document.getElementById("frm")));
  fetch("/save",{method:"POST",body:b}).then(function(r){
    r.ok?showToast("Settings saved!"):showToast("Save failed",true);});}
function doRestart(){
  if(!confirm("Restart the board now?"))return;
  fetch("/restart",{method:"POST"});showToast("Restarting...");}
function lookupCity(){
  var name=prompt("Enter city name (e.g. 'New York' or 'Paris, France'):");
  if(!name)return;
  showToast("Looking up "+name+"...");
  fetch("https://geocoding-api.open-meteo.com/v1/search?count=1&name="+encodeURIComponent(name))
    .then(function(r){return r.json();}).then(function(d){
      if(!d.results||!d.results.length){showToast("City not found",true);return;}
      var hit=d.results[0];
      document.querySelector('input[name=lat]').value=hit.latitude.toFixed(4);
      document.querySelector('input[name=lon]').value=hit.longitude.toFixed(4);
      document.querySelector('input[name=city]').value=(hit.name+(hit.admin1?", "+hit.admin1:""));
      showToast("Found: "+hit.name+", "+hit.country+" - hit Save to apply");
    }).catch(function(){showToast("Lookup failed",true);});}
</script></body></html>)HTML");
    configServer.send(200,"text/html",html);   // single buffered send (avoids chunked empty-value truncation)
}

// ================================================================
// Web Server handlers
// ================================================================
void handleRoot() { if(g_provisioning) handleWifiSetupPage(); else streamConfigPage(); }

void handleSave(){
    bool changed=false;
    auto ga=[&](const char*n,int&d,int lo,int hi){
        if(configServer.hasArg(n)){int v=configServer.arg(n).toInt();if(v>=lo&&v<=hi){d=v;changed=true;}}
    };
    ga("dayBright",cfg.dayBright,10,100);ga("nightBright",cfg.nightBright,5,100);
    ga("nightStart",cfg.nightStart,0,23);ga("nightEnd",cfg.nightEnd,0,23);
    ga("critLow",cfg.critLow,40,100);ga("critHigh",cfg.critHigh,150,400);
    if(configServer.hasArg("dashboardSec")){int v=configServer.arg("dashboardSec").toInt();if(v>=3&&v<=120){cfg.dashboardMs=v*1000;changed=true;}}
    if(configServer.hasArg("lat")){
        String v=configServer.arg("lat");float f=v.toFloat();
        if(f>=-90.0&&f<=90.0&&v.length()<=12){cfg.lat=v;changed=true;}
    }
    if(configServer.hasArg("lon")){
        String v=configServer.arg("lon");float f=v.toFloat();
        if(f>=-180.0&&f<=180.0&&v.length()<=12){cfg.lon=v;changed=true;}
    }
    if(configServer.hasArg("city")){
        String v=configServer.arg("city");
        if(v.length()>0&&v.length()<=30){cfg.city=v;changed=true;}
    }
    if(configServer.hasArg("units")){
        String v=configServer.arg("units");
        bool c=(v=="C"||v=="c");
        if(c!=cfg.isCelsius){cfg.isCelsius=c;changed=true;}
    }
    if(configServer.hasArg("tz")){
        String v=configServer.arg("tz");
        if(v.length()>0&&v.length()<=64&&v!=cfg.tzString){cfg.tzString=v;changed=true;}
    }
    // --- glucose source + Dexcom/Libre credentials ---
    bool srcChanged=false;
    if(configServer.hasArg("cgmSource")){int v=configServer.arg("cgmSource").toInt();
        if(v>=0&&v<=2&&v!=cfg.cgmSource){cfg.cgmSource=v;changed=true;srcChanged=true;}}
    auto gs=[&](const char*n,String&d,size_t mx){
        if(configServer.hasArg(n)){String v=configServer.arg(n);if(v.length()<=mx&&v!=d){d=v;changed=true;srcChanged=true;}}
    };
    gs("dexUser",cfg.dexUser,80); gs("dexRegion",cfg.dexRegion,6);
    gs("libUser",cfg.libUser,80); gs("libRegion",cfg.libRegion,6);
    gs("nsurl",cfg.nsUrl,160);
    // passwords: only overwrite when a non-empty value is submitted (blank = keep)
    if(configServer.hasArg("dexPass")){String v=configServer.arg("dexPass");if(v.length()>0&&v.length()<=80){cfg.dexPass=v;changed=true;srcChanged=true;}}
    if(configServer.hasArg("libPass")){String v=configServer.arg("libPass");if(v.length()>0&&v.length()<=80){cfg.libPass=v;changed=true;srcChanged=true;}}
    if(configServer.hasArg("nssecret")){String v=configServer.arg("nssecret");if(v.length()>0&&v.length()<=96){cfg.nsSecret=v;changed=true;srcChanged=true;}}
    if(srcChanged){s_dexSession="";s_libToken="";s_libAcct="";s_libPatient="";}  // force re-login
    // --- Home Assistant MQTT broker (blank host = disabled) ---
    bool mqttChanged=false;
    if(configServer.hasArg("mqttHost")){String v=configServer.arg("mqttHost");if(v.length()<=80&&v!=cfg.mqttHost){cfg.mqttHost=v;changed=true;mqttChanged=true;}}
    if(configServer.hasArg("mqttPort")){int v=configServer.arg("mqttPort").toInt();if(v>=1&&v<=65535&&v!=cfg.mqttPort){cfg.mqttPort=v;changed=true;mqttChanged=true;}}
    if(configServer.hasArg("mqttUser")){String v=configServer.arg("mqttUser");if(v.length()<=64&&v!=cfg.mqttUser){cfg.mqttUser=v;changed=true;mqttChanged=true;}}
    if(configServer.hasArg("mqttPass")){String v=configServer.arg("mqttPass");if(v.length()>0&&v.length()<=64){cfg.mqttPass=v;changed=true;mqttChanged=true;}}
    if(mqttChanged)mqttReconfig=true;   // fetchTask (Core 0) reconnects
    // --- WiFi STA creds (saved now; applied on next reconnect/reboot; blank pass = keep) ---
    if(configServer.hasArg("wifiSsid")){String v=configServer.arg("wifiSsid");if(v.length()<=64&&v!=cfg.wifiSsid){cfg.wifiSsid=v;changed=true;}}
    if(configServer.hasArg("wifiPass")){String v=configServer.arg("wifiPass");if(v.length()>0&&v.length()<=64){cfg.wifiPass=v;changed=true;}}
    if(changed){saveConfig();applyTimezone();fetchWeather();configServer.send(200,"text/plain","OK");}
    else configServer.send(400,"text/plain","No valid parameters");
}

void handleRestart(){configServer.send(200,"text/plain","Restarting...");delay(500);ESP.restart();}
void handleNotFound(){configServer.send(404,"text/plain","Not found");}

// ================================================================
// WiFi provisioning — SoftAP captive portal (first-run / bad creds)
// Lets a buyer set WiFi WITHOUT building from source. Entered from setup()
// when there are no usable creds or the STA join fails; never returns.
// ================================================================
void wifiScanBuild(){            // cache scanned SSIDs as <datalist> options
    int n=WiFi.scanNetworks();
    g_wifiScanOpts="";
    for(int i=0;i<n&&i<24;i++){
        String s=WiFi.SSID(i);
        if(s.length()==0)continue;                         // skip hidden
        s.replace("&","&amp;");s.replace("\"","&quot;");s.replace("<","&lt;");
        g_wifiScanOpts+="<option value=\""+s+"\">";
    }
    WiFi.scanDelete();
}
void handleWifiSetupPage(){      // captive-portal setup form (small -> single send)
    String p=R"HTML(<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Wi-Fi Setup</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:#f5f5f5;color:#222;padding:18px}
h1{font-size:1.35rem;color:#e74c3c;margin-bottom:4px}
.sub{font-size:.82rem;color:#666;margin-bottom:16px}
.card{background:#fff;border-radius:12px;box-shadow:0 1px 4px rgba(0,0,0,.1);padding:16px}
label{display:block;font-size:.9rem;margin:12px 0 4px;font-weight:600}
input{width:100%;padding:11px;border:1px solid #ddd;border-radius:8px;font-size:1rem;background:#fafafa}
button{width:100%;border:none;border-radius:10px;padding:14px;margin-top:18px;font-size:1rem;font-weight:700;background:#e74c3c;color:#fff}
.tip{font-size:.75rem;color:#888;margin-top:10px;text-align:center}
</style></head><body>
<h1>&#128246; Wi-Fi Setup</h1>
<p class="sub">Connect this glucose panel to your home Wi-Fi.</p>
<div class="card"><form method="POST" action="/wifisave">
<label>Network name (SSID)</label>
<input list="nets" name="ssid" placeholder="Pick or type your Wi-Fi" autocomplete="off" required>
<datalist id="nets">)HTML";
    p+=g_wifiScanOpts;
    p+=R"HTML(</datalist>
<label>Password</label>
<input type="password" name="pass" placeholder="Leave blank if open network">
<button type="submit">Save &amp; Connect</button>
<p class="tip">The panel reboots and joins your network. If it can't, this page reappears.</p>
</form></div></body></html>)HTML";
    configServer.send(200,"text/html",p);
}
void handleCaptiveRedirect(){    // bounce OS captive-checks to the portal root
    configServer.sendHeader("Location","http://"+WiFi.softAPIP().toString()+"/",true);
    configServer.send(302,"text/plain","");
}
void handleWifiSave(){
    String s=configServer.arg("ssid");
    String pw=configServer.arg("pass");
    if(s.length()==0||s.length()>64){configServer.send(400,"text/plain","Missing SSID");return;}
    cfg.wifiSsid=s; cfg.wifiPass=pw;                        // pw may be blank (open network)
    saveConfig();
    String body="<!DOCTYPE html><meta name=viewport content='width=device-width,initial-scale=1'>"
                "<body style='font-family:sans-serif;padding:28px;text-align:center'>"
                "<h2>Saved &mdash; rebooting&hellip;</h2><p>Joining <b>"+s+"</b>.</p></body>";
    configServer.send(200,"text/html",body);
    delay(800);
    ESP.restart();
}
void showProvisioningScreen(){   // on-panel instructions (LVGL)
    bsp_display_lock(100);
    lv_obj_clean(lv_scr_act());
    lv_obj_t *scr=lv_scr_act();
    lv_obj_set_style_bg_color(scr,lv_color_hex(0x060A14),0);
    lv_obj_set_style_bg_opa(scr,LV_OPA_COVER,0);
    lv_obj_t *t=lv_label_create(scr);
    lv_label_set_text(t,"Wi-Fi Setup");
    lv_obj_set_style_text_color(t,lv_color_hex(0x00C8FF),0);
    lv_obj_set_style_text_font(t,&lv_font_montserrat_28,0);
    lv_obj_align(t,LV_ALIGN_TOP_MID,0,34);
    lv_obj_t *m=lv_label_create(scr);
    lv_label_set_text(m,"On your phone or laptop, open\nWi-Fi settings and join:");
    lv_obj_set_style_text_color(m,lv_color_hex(0xC8D6E5),0);
    lv_obj_set_style_text_font(m,&lv_font_montserrat_16,0);
    lv_obj_set_style_text_align(m,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_align(m,LV_ALIGN_TOP_MID,0,86);
    lv_obj_t *ap=lv_label_create(scr);
    lv_label_set_text(ap,g_apName.c_str());
    lv_obj_set_style_text_color(ap,lv_color_hex(0x00FF88),0);
    lv_obj_set_style_text_font(ap,&lv_font_montserrat_22,0);
    lv_obj_align(ap,LV_ALIGN_CENTER,0,6);
    lv_obj_t *u=lv_label_create(scr);
    String _apurl="Then open:  http://"+WiFi.softAPIP().toString(); lv_label_set_text(u,_apurl.c_str());
    lv_obj_set_style_text_color(u,lv_color_hex(0xFFDD00),0);
    lv_obj_set_style_text_font(u,&lv_font_montserrat_18,0);
    lv_obj_align(u,LV_ALIGN_CENTER,0,48);
    lv_obj_t *h=lv_label_create(scr);
    lv_label_set_text(h,"The setup page usually opens by itself.");
    lv_obj_set_style_text_color(h,lv_color_hex(0x8899AA),0);
    lv_obj_set_style_text_font(h,&lv_font_montserrat_12,0);
    lv_obj_align(h,LV_ALIGN_BOTTOM_MID,0,-22);
    bsp_display_unlock();
}
void startProvisioning(){        // never returns
    g_provisioning=true;
    prefs.begin("boot",false);prefs.putInt("crashes",0);prefs.end();   // provisioning isn't a crash loop
    String mac=WiFi.macAddress(); mac.replace(":","");
    g_apName="glucoscout-"+mac.substring(8); g_apName.toLowerCase();
    wifiScanBuild();                          // scan before switching to AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(g_apName.c_str());            // open AP (no password)
    delay(300);
    IPAddress apIP=WiFi.softAPIP();
    dnsServer.start(53,"*",apIP);             // captive DNS: every name -> us
    configServer.on("/",                    HTTP_GET,  handleRoot);          // g_provisioning -> setup page
    configServer.on("/wifisave",            HTTP_POST, handleWifiSave);
    configServer.on("/generate_204",                   handleCaptiveRedirect); // Android
    configServer.on("/gen_204",                        handleCaptiveRedirect); // Android
    configServer.on("/hotspot-detect.html",            handleCaptiveRedirect); // iOS / macOS
    configServer.on("/connecttest.txt",                handleCaptiveRedirect); // Windows
    configServer.on("/ncsi.txt",                       handleCaptiveRedirect); // Windows
    configServer.onNotFound(handleCaptiveRedirect);
    configServer.begin();
    showProvisioningScreen();
    Serial.printf("[Provisioning] AP '%s' -> http://%s/\n",g_apName.c_str(),apIP.toString().c_str());
    for(;;){
        dnsServer.processNextRequest();
        configServer.handleClient();
        esp_task_wdt_reset();
        delay(5);
    }
}

void startConfigServer(){
    configServer.on("/",         HTTP_GET,  handleRoot);
    configServer.on("/save",     HTTP_POST, handleSave);
    configServer.on("/otacheck", HTTP_POST, handleOtaCheck);
    configServer.on("/otastatus",HTTP_GET,  handleOtaStatus);
    configServer.on("/restart",  HTTP_POST, handleRestart);
    configServer.onNotFound(handleNotFound);
    configServer.begin();
    Serial.print("[WebServer] http://");Serial.println(WiFi.localIP());
}

// ================================================================
// setup()
// ================================================================
void setup(){
    Serial.begin(115200);delay(500);
    prefs.begin("boot",false);
    int cc=prefs.getInt("crashes",0)+1;prefs.putInt("crashes",cc);prefs.end();
    dataMutex=xSemaphoreCreateMutex();
    bsp_display_cfg_t dcfg={.lvgl_port_cfg=ESP_LVGL_PORT_INIT_CONFIG(),.buffer_size=320*480,.rotate=LV_DISP_ROT_90};
    bsp_display_start_with_config(&dcfg);bsp_display_brightness_set(100);
    bsp_display_lock(100);
    lv_obj_t*sp=lv_label_create(lv_scr_act());lv_label_set_text(sp,"Starting up...");
    lv_obj_set_style_text_color(sp,lv_color_hex(0xFFFFFF),0);
    lv_obj_set_style_text_font(sp,&lv_font_montserrat_20,0);lv_obj_center(sp);
    bsp_display_unlock();
    loadConfig();                                    // need creds BEFORE we connect
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSsidEff().c_str(),wifiPassEff().c_str());
    for(int i=0;i<40&&WiFi.status()!=WL_CONNECTED;i++)delay(500);   // ~20s
    Serial.println(WiFi.status()==WL_CONNECTED?"WiFi OK":"WiFi FAILED");
    Serial.println("IP: "+WiFi.localIP().toString());
    // No usable WiFi creds, or couldn't join -> SoftAP captive portal (never returns)
    if(wifiSsidEff().length()==0 || WiFi.status()!=WL_CONNECTED){
        startProvisioning();
    }
    if(cc>=SAFE_MODE_CRASHES){runSafeMode();}
    ArduinoOTA.setHostname("CGM-Dashboard");ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([](){
        esp_task_wdt_delete(NULL);bsp_display_lock(100);lv_obj_clean(lv_scr_act());
        lv_obj_t*l=lv_label_create(lv_scr_act());lv_label_set_text(l,"OTA Update...\nDo not power off!");
        lv_obj_set_style_text_color(l,lv_color_hex(0xFFFFFF),0);
        lv_obj_set_style_text_font(l,&lv_font_montserrat_20,0);lv_obj_center(l);bsp_display_unlock();
    });
    ArduinoOTA.onEnd([](){Serial.println("OTA done");});
    ArduinoOTA.onError([](ota_error_t e){Serial.println("OTA err: "+String(e));});
    ArduinoOTA.begin();
    startConfigServer();
    configTime(0,0,"pool.ntp.org","time.nist.gov");
    applyTimezone();
    bsp_display_lock(100);lv_obj_clean(lv_scr_act());createDashboardUI();
    lv_obj_add_event_cb(lv_scr_act(),screenLongPress_cb,LV_EVENT_LONG_PRESSED,NULL);
    bsp_display_unlock();
    fetchGlucose();fetchWeather();
    bsp_display_lock(100);updateDashboardUI();bsp_display_unlock();
    xTaskCreatePinnedToCore(fetchTask,"fetchTask",16384,NULL,1,NULL,0);
    prefs.begin("boot",false);prefs.putInt("crashes",0);prefs.end();
    Serial.println("Boot OK");
    esp_task_wdt_config_t wc={.timeout_ms=30000,.idle_core_mask=0,.trigger_panic=true};
    esp_task_wdt_init(&wc);esp_task_wdt_add(NULL);
}

// ================================================================
// loop()
// ================================================================
void loop(){
    unsigned long now=millis();
    esp_task_wdt_reset();ArduinoOTA.handle();configServer.handleClient();
    static unsigned long lH=0;
    // MaxAlloc = largest contiguous INTERNAL block (ESP.getMaxAllocHeap). If this shrinks
    // toward ~16-32KB while Heap stays high, the freeze is internal-heap FRAGMENTATION
    // (no contiguous block left for the next SSL buffer / DynamicJsonDocument) — not a leak.
    if(now-lH>=60000){lH=now;
        size_t maxAlloc=ESP.getMaxAllocHeap();
        Serial.println("Heap: "+String(ESP.getFreeHeap())+" MaxAlloc: "+String(maxAlloc)+" PSRAM: "+String(ESP.getFreePsram()));
        // Safety net: if the largest contiguous INTERNAL block falls below what the next TLS
        // handshake needs, reboot cleanly (~5s) before a hard freeze. With JSON in PSRAM this
        // should rarely trigger; reset the crash counter so it's not mistaken for a crash.
        if(maxAlloc<24000){
            Serial.println("[heap] MaxAlloc low -> graceful reboot");
            prefs.begin("boot",false);prefs.putInt("crashes",0);prefs.end();
            delay(200);ESP.restart();
        }
    }
    if(ns_data_ready||wx_data_ready||gmi_ready){
        ns_data_ready=false;wx_data_ready=false;gmi_ready=false;
        if(!inSettings&&bsp_display_lock(200)){
            updateDashboardUI();
            bsp_display_unlock();
        }
    }
    if(!inSettings){
        static unsigned long lC=0;
        if(now-lC>=1000){lC=now;
            struct tm ti;
            if(getLocalTime(&ti)){
                char tb[20],db[30];
                strftime(tb,sizeof(tb),"%I:%M %p",&ti);strftime(db,sizeof(db),"%a, %b %d",&ti);
                if(bsp_display_lock(200)){lv_label_set_text(lbl_time,tb);lv_label_set_text(lbl_date,db);bsp_display_unlock();}
                static int lHr=-1;if(ti.tm_hour!=lHr){lHr=ti.tm_hour;applyBrightness(ti.tm_hour);}
            }
        }
    }
    delay(10);
}

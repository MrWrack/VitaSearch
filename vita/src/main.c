#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/sysmodule.h>
#include <vita2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "net.h"
#include "config.h"
#include "spotify.h"
#include "settings.h"
#include "spotify_status.h"

#define SCREEN_W 960
#define SCREEN_H 544
#define INPUT_MAX 255
#define RESULT_MAX 8

typedef enum { MODE_WEB=0, MODE_SPOTIFY=1, MODE_KEYBOARD=2, MODE_SETTINGS=3 } AppMode;
static char proxy[256], api_key[128]="", ca_file[256]="", session[96]="";
static int cursor_x=SCREEN_W/2,cursor_y=SCREEN_H/2;
static vita2d_texture *frame=NULL,*cover=NULL;
static vita2d_pgf *font=NULL;
static SpotifyState sp;
static SpotifyTrack results[RESULT_MAX];
static int result_count=0,result_selected=0,search_view=0;
static int settings_selected=0,javascript_enabled=1;
static char settings_status[96]="";
static char last_cover[512]="";
static int online=0;
static int net_proxy_ok=0;
static int net_internet_ok=0;
static int net_https_ok=0;
static int net_latency_ms=-1;
static int use_selected_search=1;
static int search_engine_index=0;
static VitaSpotifyStatus spotify_status;
static int spotify_status_rc=-1;

static unsigned int net_last_check=0;

static int browser_touch_down=0;
static int search_keyboard_open=0;
static char search_text[512]="";
typedef struct { char title[48]; } BrowserTab;
#define TAB_MAX 6
static BrowserTab browser_tabs[TAB_MAX];
static int browser_tab_count=1;
static int browser_tab_active=0;
static int browser_tab_focus=0;


static int refresh_frame(void);
static int network_probe(void);

static const char *keys[]={"1","2","3","4","5","6","7","8","9","0","q","w","e","r","t","y","u","i","o","p","a","s","d","f","g","h","j","k","l",".","z","x","c","v","b","n","m","/","-","_",":","?","&","=","%","+","@","#"," ","<","GO"};
#define KEY_COUNT ((int)(sizeof(keys)/sizeof(keys[0])))
#define KEY_COLS 10

static void draw_text(float x,float y,unsigned int c,float scale,const char*t){if(font)vita2d_pgf_draw_text(font,x,y,c,scale,t?t:"");}
static void json_escape(const char *src,char *dst,size_t cap){size_t j=0;for(size_t i=0;src[i]&&j+2<cap;i++){unsigned char c=src[i];if(c=='"'||c=='\\')dst[j++]='\\';if(c>=32)dst[j++]=c;}dst[j]=0;}
static int extract_session(const char*json){const char*p=strstr(json,"\"session\":\"");if(!p)return-1;p+=11;const char*e=strchr(p,'"');if(!e)return-1;size_t n=e-p;if(n>=sizeof(session))n=sizeof(session)-1;memcpy(session,p,n);session[n]=0;return 0;}
static int post_web(const char*path,const char*json){char url[512];snprintf(url,sizeof(url),"%s%s",proxy,path);NetBuffer b;int rc=net_post_json(url,json,&b);if(rc==0&&b.data)extract_session((char*)b.data);net_buffer_free(&b);return rc;}
static int create_session(void){char u[512];snprintf(u,sizeof(u),"%s/session",proxy);NetBuffer b;if(net_post_json(u,"{}",&b)!=0)return-1;int rc=extract_session((char*)b.data);net_buffer_free(&b);return rc;}
static const char *search_engine_id(void){
 if(search_engine_index==1)return "bing";
 if(search_engine_index==2)return "duckduckgo";
 return "google";
}
static const char *search_engine_name(void){
 if(search_engine_index==1)return "Bing";
 if(search_engine_index==2)return "DuckDuckGo";
 return "Google";
}
static int open_target(const char*t){
 char e[768],b[1152];
 json_escape(t,e,sizeof(e));
 snprintf(b,sizeof(b),"{\"session\":\"%s\",\"url\":\"%s\",\"search_engine\":\"%s\",\"use_selected_search\":%s}",
  session,e,search_engine_id(),use_selected_search?"true":"false");
 return post_web("/open",b);
}
static int remote_simple(const char*p){char b[160];snprintf(b,sizeof(b),"{\"session\":\"%s\"}",session);return post_web(p,b);}
static int remote_click(int x,int y){char b[256];snprintf(b,sizeof(b),"{\"session\":\"%s\",\"x\":%d,\"y\":%d}",session,x,y);return post_web("/click",b);}
static int remote_scroll(int dx,int dy){char b[256];snprintf(b,sizeof(b),"{\"session\":\"%s\",\"dx\":%d,\"dy\":%d}",session,dx,dy);return post_web("/scroll",b);}

static int tab_new(void){
 if(browser_tab_count>=TAB_MAX)return -1;
 char b[160];snprintf(b,sizeof(b),"{\"session\":\"%s\"}",session);
 if(post_web("/tabs/new",b)<0)return -1;
 browser_tab_count++;browser_tab_active=browser_tab_count-1;browser_tab_focus=browser_tab_active;
 snprintf(browser_tabs[browser_tab_active].title,sizeof(browser_tabs[browser_tab_active].title),"New");
 refresh_frame();return 0;
}
static int tab_select(int index){
 if(index<0||index>=browser_tab_count)return -1;
 char b[192];snprintf(b,sizeof(b),"{\"session\":\"%s\",\"index\":%d}",session,index);
 if(post_web("/tabs/select",b)<0)return -1;
 browser_tab_active=index;browser_tab_focus=index;refresh_frame();return 0;
}
static int tab_close(int index){
 if(index<0||index>=browser_tab_count)return -1;
 char b[192];snprintf(b,sizeof(b),"{\"session\":\"%s\",\"index\":%d}",session,index);
 if(post_web("/tabs/close",b)<0)return -1;
 if(browser_tab_count>1)browser_tab_count--;
 if(browser_tab_active>=browser_tab_count)browser_tab_active=browser_tab_count-1;
 if(browser_tab_focus>=browser_tab_count)browser_tab_focus=browser_tab_count-1;
 refresh_frame();return 0;
}
static int refresh_frame(void){char u[512];snprintf(u,sizeof(u),"%s/frame?session=%s",proxy,session);NetBuffer b;if(net_get(u,&b)!=0)return-1;vita2d_texture*t=vita2d_load_PNG_buffer(b.data);net_buffer_free(&b);if(!t)return-1;if(frame)vita2d_free_texture(frame);frame=t;return 0;}
static void append_input(char*i,const char*p){if(strlen(i)+strlen(p)<INPUT_MAX)strcat(i,p);}

static void refresh_spotify_status(void){
 spotify_status_rc=spotify_status_fetch(proxy,&spotify_status);
}

static const char *settings_categories[]={
 "Browser",
 "Default Search",
 "Network",
 "Privacy",
 "Clear data",
 "Proxy / HTTPS",
 "Spotify",
 "Controls",
 "Appearance",
 "About VitaSearch"
};
#define SETTINGS_CATEGORY_COUNT ((int)(sizeof(settings_categories)/sizeof(settings_categories[0])))

static const char *clear_items[]={
 "Clear cookies",
 "Clear search history",
 "Clear browser history",
 "Clear cache",
 "Clear site data",
 "Clear all browser data"
};
#define CLEAR_COUNT ((int)(sizeof(clear_items)/sizeof(clear_items[0])))

static int settings_page=0; /* 0 main, 1 browser, 2 default search, 3 network, 4 privacy, 5 clear, 6 proxy, 7 spotify, 8 controls, 9 appearance, 10 about */

static void draw_settings_row(int y,const char *label,int selected,const char *value){
 unsigned int bg=selected?RGBA8(27,72,47,255):RGBA8(22,27,32,255);
 vita2d_draw_rectangle(28,y,904,39,bg);
 draw_text(44,y+27,RGBA8(242,245,244,255),0.72f,label);
 if(value&&value[0])draw_text(710,y+27,RGBA8(35,235,110,255),0.66f,value);
}

static void draw_settings_header(const char *title){
 vita2d_draw_rectangle(0,0,960,544,RGBA8(9,12,15,255));
 draw_text(28,40,RGBA8(35,235,110,255),1.12f,title);
 draw_text(822,38,RGBA8(165,180,175,255),0.60f,"O Back");
}

static void draw_settings_main(void){
 draw_settings_header("VitaSearch - Settings");
 for(int i=0;i<SETTINGS_CATEGORY_COUNT;i++)draw_settings_row(62+i*45,settings_categories[i],i==settings_selected,"");
 draw_text(28,505,RGBA8(135,150,145,255),0.60f,"D-pad Navigate   X Select");
}

static void draw_settings_browser(void){
 draw_settings_header("Browser");
 draw_settings_row(78,"JavaScript",settings_selected==0,javascript_enabled?"ON":"OFF");
 draw_text(44,150,RGBA8(165,180,175,255),0.66f,"Search engine: Google");
 draw_text(44,184,RGBA8(165,180,175,255),0.66f,"Tabs: up to 6");
 draw_text(44,218,RGBA8(165,180,175,255),0.66f,"Touch search bar opens keyboard");
}

static void draw_settings_default_search(void){
 draw_settings_header("Default Search");
 draw_settings_row(78,"Use VitaSearch search",settings_selected==0,use_selected_search?"ON":"OFF");
 draw_settings_row(126,"Search engine",settings_selected==1,search_engine_name());
 draw_text(44,206,RGBA8(165,180,175,255),0.64f,"ON: plain text uses the selected search engine.");
 draw_text(44,242,RGBA8(165,180,175,255),0.64f,"OFF: uses the original Google fallback.");
 draw_text(44,300,RGBA8(235,180,80,255),0.58f,"Does not patch the PS Vita system browser.");
 draw_text(44,338,RGBA8(135,150,145,255),0.58f,"This setting is the default search inside VitaSearch.");
}

static void draw_settings_network(void){
 draw_settings_header("Network");
 draw_settings_row(78,"Vita / Proxy",0,net_proxy_ok?"CONNECTED":"OFFLINE");
 draw_settings_row(126,"Internet",0,net_internet_ok?"ONLINE":"NO ACCESS");
 draw_settings_row(174,"HTTPS",0,net_https_ok?"ON":"OFF");
 char ping[64];if(net_latency_ms>=0)snprintf(ping,sizeof(ping),"%d ms",net_latency_ms);else snprintf(ping,sizeof(ping),"--");
 draw_settings_row(222,"Response time",0,ping);
 draw_text(44,300,RGBA8(165,180,175,255),0.66f,proxy);
 draw_text(44,340,RGBA8(135,150,145,255),0.60f,"Green: proxy + internet   Yellow: proxy only   Red: offline");
}

static void draw_settings_privacy(void){
 draw_settings_header("Privacy");
 draw_text(44,92,RGBA8(242,245,244,255),0.72f,"Cookies and site data stay in the proxy browser context.");
 draw_text(44,136,RGBA8(242,245,244,255),0.72f,"Spotify OAuth tokens stay on the proxy.");
 draw_text(44,180,RGBA8(242,245,244,255),0.72f,"Use Clear data to remove browser information.");
}

static void draw_settings_clear(void){
 draw_settings_header("Clear data");
 for(int i=0;i<CLEAR_COUNT;i++)draw_settings_row(72+i*52,clear_items[i],i==settings_selected,"");
 if(settings_status[0])draw_text(28,420,RGBA8(35,235,110,255),0.64f,settings_status);
 if(settings_selected==5)draw_text(28,466,RGBA8(235,180,80,255),0.60f,"Warning: clears cookies, history, cache and site data.");
 draw_text(28,510,RGBA8(135,150,145,255),0.60f,"X Clear   O Back");
}

static void draw_settings_proxy(void){
 draw_settings_header("Proxy / HTTPS");
 draw_settings_row(78,"HTTPS connection",0,!strncmp(proxy,"https://",8)?"ON":"OFF");
 draw_text(44,145,RGBA8(242,245,244,255),0.68f,"Proxy address:");
 draw_text(44,180,RGBA8(35,235,110,255),0.64f,proxy);
 draw_text(44,228,RGBA8(165,180,175,255),0.62f,"TLS certificate verification remains enabled.");
}

static void draw_settings_spotify(void){
 draw_settings_header("Spotify");
 draw_settings_row(78,"Spotify",0,(spotify_status_rc==0&&spotify_status.connected)?"CONNECTED":"NOT CONNECTED");
 draw_settings_row(126,"Callback",0,spotify_status.callback[0]?spotify_status.callback:"unknown");
 draw_settings_row(174,"Token",0,spotify_status.token[0]?spotify_status.token:"unknown");
 char dev[144];
 if(spotify_status.device_type[0])snprintf(dev,sizeof(dev),"%s (%s)",spotify_status.device,spotify_status.device_type);
 else snprintf(dev,sizeof(dev),"%s",spotify_status.device[0]?spotify_status.device:"None");
 draw_settings_row(222,"Device",0,dev);
 draw_text(44,300,RGBA8(165,180,175,255),0.62f,"Touch: Previous / Play-Pause / Next / Seek / Volume");
 draw_text(44,338,RGBA8(165,180,175,255),0.62f,"OAuth tokens stay on the proxy.");
}

static void draw_settings_controls(void){
 draw_settings_header("Controls");
 const char *rows[]={
  "X        Select / click",
  "O        Back / close",
  "Square   Open keyboard",
  "Triangle Search / submit",
  "D-pad Up/Down   Scroll",
  "D-pad Left/Right   Switch tabs",
  "L-stick  Move mouse pointer",
  "L / R    Browser back / forward"
 };
 for(int i=0;i<8;i++)draw_text(48,86+i*38,RGBA8(242,245,244,255),0.70f,rows[i]);
 draw_text(48,408,RGBA8(35,235,110,255),0.66f,"TOUCH");
 draw_text(48,444,RGBA8(242,245,244,255),0.68f,"Links, tabs, search bar and Spotify controls");
}

static void draw_settings_appearance(void){
 draw_settings_header("Appearance");
 draw_settings_row(78,"Theme",0,"DARK");
 draw_settings_row(126,"Accent",0,"NEON GREEN");
 draw_text(44,196,RGBA8(165,180,175,255),0.64f,"Designed for the PS Vita 960 x 544 display.");
}

static void draw_settings_about(void){
 draw_settings_header("About VitaSearch");
 draw_text(44,92,RGBA8(35,235,110,255),0.90f,"VitaSearch v0.99 RC17");
 draw_text(44,142,RGBA8(242,245,244,255),0.68f,"Modern web rendering through Chromium proxy.");
 draw_text(44,180,RGBA8(242,245,244,255),0.68f,"PS Vita native controls + touch.");
 draw_text(44,218,RGBA8(242,245,244,255),0.68f,"Spotify Connect integration.");
}

static void draw_settings(void){
 switch(settings_page){
  case 1:draw_settings_browser();break;
  case 2:draw_settings_default_search();break;
  case 3:draw_settings_network();break;
  case 4:draw_settings_privacy();break;
  case 5:draw_settings_clear();break;
  case 6:draw_settings_proxy();break;
  case 7:draw_settings_spotify();break;
  case 8:draw_settings_controls();break;
  case 9:draw_settings_appearance();break;
  case 10:draw_settings_about();break;
  default:draw_settings_main();break;
 }
}

static void settings_action(void){
 settings_status[0]=0;
 if(settings_page==0){settings_page=settings_selected+1;settings_selected=0;if(settings_page==3)network_probe();if(settings_page==7)refresh_spotify_status();return;}
 if(settings_page==1){
   int next=!javascript_enabled;
   if(settings_set_javascript(proxy,session,next)==0){javascript_enabled=next;snprintf(settings_status,sizeof(settings_status),"JavaScript %s",next?"enabled":"disabled");}
   else snprintf(settings_status,sizeof(settings_status),"Could not change JavaScript");
   return;
 }
 if(settings_page==2){
   if(settings_selected==0)use_selected_search=!use_selected_search;
   else search_engine_index=(search_engine_index+1)%3;
   return;
 }
 if(settings_page==5){
   const char *what="cookies";
   if(settings_selected==1)what="search";
   else if(settings_selected==2)what="history";
   else if(settings_selected==3)what="cache";
   else if(settings_selected==4)what="site-data";
   else if(settings_selected==5)what="all";
   if(settings_clear_data(proxy,session,what)==0){
     snprintf(settings_status,sizeof(settings_status),"Cleared: %s",what);
     if(settings_selected==2||settings_selected==5)refresh_frame();
   }else snprintf(settings_status,sizeof(settings_status),"Clear failed: %s",what);
 }
}


static void update_cover(void){if(strcmp(last_cover,sp.cover_url)==0)return;strncpy(last_cover,sp.cover_url,sizeof(last_cover)-1);last_cover[sizeof(last_cover)-1]=0;if(cover){vita2d_free_texture(cover);cover=NULL;}if(sp.cover_url[0])cover=spotify_load_cover(proxy,sp.cover_url);}
static void spotify_refresh(void){if(spotify_get_state(proxy,&sp)==0)update_cover();}
static void spotify_login_web(void){char u[768];snprintf(u,sizeof(u),"%s/spotify/login?key=%s",proxy,api_key);open_target(u);refresh_frame();}

static void keyboard_draw(const char*input,int sel,const char*title){vita2d_draw_rectangle(0,0,960,544,RGBA8(10,12,16,255));draw_text(30,30,RGBA8(40,240,120,255),1.0f,title);vita2d_draw_rectangle(28,46,904,58,RGBA8(25,30,38,255));draw_text(42,84,RGBA8(235,245,240,255),1.0f,input[0]?input:"Type...");for(int i=0;i<KEY_COUNT;i++){int r=i/KEY_COLS,c=i%KEY_COLS;float x=30+c*90,y=126+r*64,w=i==KEY_COUNT-1?180:78;vita2d_draw_rectangle(x,y,w,49,i==sel?RGBA8(30,230,110,255):RGBA8(38,44,54,255));draw_text(x+22,y+33,i==sel?RGBA8(0,20,10,255):RGBA8(240,240,240,255),0.9f,keys[i]);}draw_text(30,527,RGBA8(170,180,190,255),0.72f,"D-pad move   X type/select   O close   Triangle search");}

static void draw_progress(int y){float pct=sp.duration_ms>0?(float)sp.progress_ms/sp.duration_ms:0;if(pct<0)pct=0;if(pct>1)pct=1;vita2d_draw_rectangle(250,y,650,8,RGBA8(45,55,62,255));vita2d_draw_rectangle(250,y,650*pct,8,RGBA8(35,235,110,255));}
static void draw_mini_player(void){vita2d_draw_rectangle(0,468,960,76,RGBA8(11,15,18,245));if(cover)vita2d_draw_texture_scale(cover,12,474,0.20f,0.20f);draw_text(86,495,RGBA8(245,245,245,255),0.80f,sp.title[0]?sp.title:"Spotify");draw_text(86,520,RGBA8(165,180,170,255),0.65f,sp.artist);draw_text(700,507,RGBA8(35,235,110,255),0.78f,sp.playing?"X Pause":"X Play");draw_text(815,507,RGBA8(190,200,195,255),0.65f,"START Spotify  SELECT Settings");}

static void draw_spotify(void){vita2d_draw_rectangle(0,0,960,544,RGBA8(7,10,9,255));draw_text(28,38,RGBA8(35,235,110,255),1.25f,"VitaSearch Spotify");draw_text(735,36,RGBA8(160,175,165,255),0.65f,"START: Web");
 if(!sp.connected){draw_text(70,180,RGBA8(245,245,245,255),1.15f,"Spotify is not connected");draw_text(70,225,RGBA8(170,185,175,255),0.8f,"Press SELECT to open secure Spotify login through the proxy.");draw_text(70,270,RGBA8(35,235,110,255),0.9f,"SELECT  Connect Spotify");return;}
 if(search_view){draw_text(30,78,RGBA8(220,230,225,255),0.78f,"SEARCH RESULTS    Triangle: new search    O: Now Playing    SELECT: add to queue");for(int i=0;i<result_count;i++){int y=103+i*45;vita2d_draw_rectangle(24,y,912,39,i==result_selected?RGBA8(30,75,48,255):RGBA8(20,25,23,255));draw_text(38,y+25,RGBA8(245,245,245,255),0.74f,results[i].name);draw_text(520,y+25,RGBA8(150,170,158,255),0.62f,results[i].artist);}if(!result_count)draw_text(35,150,RGBA8(180,190,185,255),0.8f,"No results. Press Triangle to search.");draw_mini_player();return;}
 if(cover)vita2d_draw_texture_scale(cover,40,88,0.72f,0.72f);else vita2d_draw_rectangle(40,88,240,240,RGBA8(25,32,28,255));draw_text(320,110,RGBA8(150,170,158,255),0.65f,"NOW PLAYING");draw_text(320,154,RGBA8(245,245,245,255),1.12f,sp.title[0]?sp.title:"No active playback");draw_text(320,188,RGBA8(175,190,180,255),0.82f,sp.artist);draw_text(320,222,RGBA8(135,155,143,255),0.68f,sp.device);
 draw_progress(260);char info[128];snprintf(info,sizeof(info),"Seek: Left/Right 10s     Volume: %d%% (Up/Down)",sp.volume);draw_text(320,294,RGBA8(175,190,180,255),0.68f,info);
 vita2d_draw_rectangle(320,330,150,54,RGBA8(28,35,31,255));vita2d_draw_rectangle(485,330,150,54,RGBA8(35,235,110,255));vita2d_draw_rectangle(650,330,150,54,RGBA8(28,35,31,255));draw_text(352,364,RGBA8(235,240,238,255),0.78f,"L  Previous");draw_text(515,364,RGBA8(5,25,12,255),0.78f,sp.playing?"X  Pause":"X  Play");draw_text(684,364,RGBA8(235,240,238,255),0.78f,"R  Next");draw_text(320,426,RGBA8(35,235,110,255),0.75f,"Triangle: Search     Square: Refresh     SELECT: Login/device page");draw_mini_player();}



static void open_search_keyboard(AppMode *mode,AppMode *return_mode,char *input,int *keysel){
 *return_mode=MODE_WEB;
 *mode=MODE_KEYBOARD;
 *keysel=0;
 search_keyboard_open=1;
 strncpy(input,search_text,511);
 input[511]=0;
}
static void close_search_keyboard(AppMode *mode,AppMode return_mode,char *input){
 strncpy(search_text,input,511);
 search_text[511]=0;
 search_keyboard_open=0;
 *mode=return_mode;
}
static void submit_search_keyboard(AppMode *mode,AppMode return_mode,char *input){
 strncpy(search_text,input,511);
 search_text[511]=0;
 if(input[0]){open_target(input);refresh_frame();}
 search_keyboard_open=0;
 *mode=return_mode;
}


static size_t net_write_cb(char *ptr,size_t size,size_t nmemb,void *userdata){
 size_t n=size*nmemb;
 char *dst=(char*)userdata;
 size_t cur=strlen(dst);
 size_t cap=1023;
 if(cur<cap){
   size_t copy=n;
   if(cur+copy>cap)copy=cap-cur;
   memcpy(dst+cur,ptr,copy);
   dst[cur+copy]=0;
 }
 return n;
}

static int network_probe(void){
 char url[768];
 snprintf(url,sizeof(url),"%s/health",proxy);

 CURL *c=curl_easy_init();
 if(!c)return -1;

 char response[1024]={0};
 struct curl_slist *headers=NULL;
 if(api_key[0]){
   char h[512];snprintf(h,sizeof(h),"X-VitaSearch-Key: %s",api_key);
   headers=curl_slist_append(headers,h);
 }

 curl_easy_setopt(c,CURLOPT_URL,url);
 curl_easy_setopt(c,CURLOPT_TIMEOUT_MS,4000L);
 curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT_MS,2500L);
 curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
 curl_easy_setopt(c,CURLOPT_HTTPHEADER,headers);
 curl_easy_setopt(c,CURLOPT_WRITEDATA,response);
 curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,net_write_cb);

 unsigned int t0=sceKernelGetProcessTimeLow();
 CURLcode rc=curl_easy_perform(c);
 unsigned int t1=sceKernelGetProcessTimeLow();

 if(headers)curl_slist_free_all(headers);
 curl_easy_cleanup(c);

 net_latency_ms=(int)((t1-t0)/1000);
 if(rc!=CURLE_OK){
   net_proxy_ok=0;net_internet_ok=0;
   net_https_ok=!strncmp(proxy,"https://",8);
   return -1;
 }

 net_proxy_ok=1;
 net_https_ok=strstr(response,"\"https\":true")!=NULL || !strncmp(proxy,"https://",8);
 net_internet_ok=strstr(response,"\"internet\":true")!=NULL;
 return 0;
}

static void draw_browser_chrome(void){
 vita2d_draw_rectangle(0,0,960,34,RGBA8(12,16,18,248));
 for(int i=0;i<browser_tab_count;i++){
   int x=14+i*132;
   unsigned int col=i==browser_tab_active?RGBA8(35,235,110,255):RGBA8(190,205,198,255);
   char tb[64];snprintf(tb,sizeof(tb),"[%s x]",browser_tabs[i].title[0]?browser_tabs[i].title:"Tab");
   draw_text(x,24,col,0.56f,tb);
 }
 if(browser_tab_count<TAB_MAX)draw_text(820,24,RGBA8(35,235,110,255),0.66f,"[ + ]");
 vita2d_draw_rectangle(0,34,960,48,RGBA8(18,23,27,248));
 vita2d_draw_rectangle(82,42,720,31,RGBA8(28,34,39,255));
 draw_text(98,64,RGBA8(190,202,196,255),0.62f,search_text[0]?search_text:"Search Google or type URL...");
 draw_text(18,64,RGBA8(190,205,198,255),0.68f,"<  >");
 {
   char ns[96];
   unsigned int nc=RGBA8(235,90,90,255);
   if(net_proxy_ok&&net_internet_ok){snprintf(ns,sizeof(ns),"Online%s %d ms",net_https_ok?" HTTPS":"",net_latency_ms);nc=RGBA8(35,235,110,255);}
   else if(net_proxy_ok){snprintf(ns,sizeof(ns),"Proxy online");nc=RGBA8(235,180,80,255);}
   else snprintf(ns,sizeof(ns),"Offline");
   draw_text(790,64,nc,0.54f,ns);
 }
}

static void browser_touch(const SceTouchData *td, AppMode *mode, AppMode *return_mode, char *input, int *keysel){
 int down=td&&td->reportNum>0;
 if(down&&!browser_touch_down){
   int x=td->report[0].x*960/1920;
   int y=td->report[0].y*544/1088;

   if(y<34){
     if(x>=810){tab_new();}
     else{
       int ti=x/132;
       if(ti>=0&&ti<browser_tab_count){
         int local=x-(ti*132);
         if(local>100)tab_close(ti);
         else tab_select(ti);
       }
     }
   }
   /* Search/address bar: tap opens keyboard. */
   else if(y>=34&&y<=82){
     open_search_keyboard(mode,return_mode,input,keysel);
   } else if(y<468){
     /* Web content: touch acts as mouse click. */
     cursor_x=x;
     cursor_y=y;
     remote_click(x,y);
     refresh_frame();
   }
 }
 browser_touch_down=down;
}


static int keyboard_touch_down=0;
static void keyboard_touch_dismiss(const SceTouchData *td,AppMode *mode,AppMode return_mode,char *input){
 int down=td&&td->reportNum>0;
 if(down&&!keyboard_touch_down){
   int x=td->report[0].x*960/1920;
   int y=td->report[0].y*544/1088;
   if(!(x>=82&&x<=802&&y>=34&&y<=82))close_search_keyboard(mode,return_mode,input);
 }
 keyboard_touch_down=down;
}

static int spotify_touch_down=0,spotify_seek_drag=0,spotify_volume_drag=0;
static void spotify_touch(const SceTouchData*t){
 int down=t&&t->reportNum>0;
 if(!down){spotify_touch_down=0;spotify_seek_drag=0;spotify_volume_drag=0;return;}
 int x=t->report[0].x*960/1920,y=t->report[0].y*544/1088;
 if(!spotify_touch_down){
  if(y>=372&&y<=452){
   if(x>=250&&x<390){spotify_command(proxy,"previous");spotify_refresh();}
   else if(x>=390&&x<570){spotify_command(proxy,sp.playing?"pause":"play");spotify_refresh();}
   else if(x>=570&&x<=710){spotify_command(proxy,"next");spotify_refresh();}
  }
  if(y>=320&&y<=356&&x>=120&&x<=840)spotify_seek_drag=1;
  if(y>=472&&y<=520&&x>=610&&x<=900)spotify_volume_drag=1;
 }
 if(spotify_seek_drag){
  if(x<120)x=120;if(x>840)x=840;
  int target=sp.duration_ms*((x-120)*100/(840-120))/100;
  spotify_seek(proxy,target);sp.progress_ms=target;
 }
 if(spotify_volume_drag){
  if(x<610)x=610;if(x>900)x=900;
  int vol=(x-610)*100/(900-610);
  spotify_volume(proxy,vol);sp.volume=vol;
 }
 spotify_touch_down=1;
}


static void draw_spotify_touch_controls(void){
 vita2d_draw_rectangle(120,330,720,8,RGBA8(55,65,62,255));
 if(sp.duration_ms>0){int w=(int)((long long)720*sp.progress_ms/sp.duration_ms);if(w<0)w=0;if(w>720)w=720;vita2d_draw_rectangle(120,330,w,8,RGBA8(35,235,110,255));}
 vita2d_draw_rectangle(250,382,140,58,RGBA8(20,30,27,220));
 vita2d_draw_rectangle(400,372,160,78,RGBA8(27,72,47,230));
 vita2d_draw_rectangle(570,382,140,58,RGBA8(20,30,27,220));
 draw_text(292,419,RGBA8(240,245,242,255),0.72f,"PREV");
 draw_text(438,418,RGBA8(35,235,110,255),0.76f,sp.playing?"PAUSE":"PLAY");
 draw_text(614,419,RGBA8(240,245,242,255),0.72f,"NEXT");
 draw_text(535,502,RGBA8(150,165,160,255),0.54f,"VOL");
 vita2d_draw_rectangle(610,488,290,8,RGBA8(55,65,62,255));
 int vw=sp.volume*290/100;if(vw<0)vw=0;if(vw>290)vw=290;
 vita2d_draw_rectangle(610,488,vw,8,RGBA8(35,235,110,255));
}

int main(void){sceSysmoduleLoadModule(SCE_SYSMODULE_NET);sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);vita2d_init();font=vita2d_load_default_pgf();sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,SCE_TOUCH_SAMPLING_STATE_START);config_load(proxy,sizeof(proxy),api_key,sizeof(api_key),ca_file,sizeof(ca_file));net_init();net_set_api_key(api_key);net_set_ca_file(ca_file);online=create_session()==0;
 memset(&spotify_status,0,sizeof(spotify_status));
 snprintf(spotify_status.callback,sizeof(spotify_status.callback),"unknown");
 snprintf(spotify_status.token,sizeof(spotify_status.token),"unknown");
 snprintf(spotify_status.device,sizeof(spotify_status.device),"None");
 if(online){network_probe();refresh_spotify_status();}
 snprintf(browser_tabs[0].title,sizeof(browser_tabs[0].title),"Google");if(online){open_target("google.com");refresh_frame();spotify_refresh();}
 AppMode mode=MODE_WEB,return_mode=MODE_WEB;unsigned int old=0;int counter=0,keysel=0;char input[INPUT_MAX+1]="";
 for(;;){SceCtrlData pad;sceCtrlPeekBufferPositive(0,&pad,1);unsigned int pressed=pad.buttons&~old;old=pad.buttons;if((pad.buttons&SCE_CTRL_START)&&(pad.buttons&SCE_CTRL_SELECT))break;
  if(mode==MODE_KEYBOARD){int row=keysel/KEY_COLS,col=keysel%KEY_COLS;if(pressed&SCE_CTRL_LEFT)col=(col+9)%10;if(pressed&SCE_CTRL_RIGHT)col=(col+1)%10;if(pressed&SCE_CTRL_UP)row=row>0?row-1:5;if(pressed&SCE_CTRL_DOWN)row=row<5?row+1:0;keysel=row*10+col;if(keysel>=KEY_COUNT)keysel=KEY_COUNT-1;if(pressed&SCE_CTRL_CIRCLE){if(return_mode==MODE_WEB)close_search_keyboard(&mode,return_mode,input);else mode=return_mode;}else if(pressed&SCE_CTRL_TRIANGLE){if(return_mode==MODE_WEB)submit_search_keyboard(&mode,return_mode,input);else{if(input[0]){result_count=spotify_search(proxy,input,results,RESULT_MAX);result_selected=0;search_view=1;spotify_refresh();}mode=return_mode;}}else if(pressed&SCE_CTRL_CROSS){if(keysel==KEY_COUNT-1){if(return_mode==MODE_WEB)submit_search_keyboard(&mode,return_mode,input);else{if(input[0]){result_count=spotify_search(proxy,input,results,RESULT_MAX);result_selected=0;search_view=1;spotify_refresh();}mode=return_mode;}}else if(!strcmp(keys[keysel],"<")){size_t n=strlen(input);if(n)input[n-1]=0;}else append_input(input,keys[keysel]);}if(mode==MODE_KEYBOARD&&return_mode==MODE_WEB){SceTouchData ktd;sceTouchPeek(SCE_TOUCH_PORT_FRONT,&ktd,1);keyboard_touch_dismiss(&ktd,&mode,return_mode,input);}}
  else if(mode==MODE_WEB){if(pressed&SCE_CTRL_START){mode=MODE_SPOTIFY;spotify_refresh();}else if(pressed&SCE_CTRL_SELECT){mode=MODE_SETTINGS;settings_page=0;settings_selected=0;settings_status[0]=0;}else if(online){int ax=(int)pad.lx-128,ay=(int)pad.ly-128;if(ax>20||ax<-20)cursor_x+=ax/22;if(ay>20||ay<-20)cursor_y+=ay/22;if(cursor_x<0)cursor_x=0;if(cursor_x>959)cursor_x=959;if(cursor_y<82)cursor_y=82;if(cursor_y>467)cursor_y=467;if(pressed&SCE_CTRL_UP){remote_scroll(0,-360);refresh_frame();}if(pressed&SCE_CTRL_DOWN){remote_scroll(0,360);refresh_frame();}if(pressed&SCE_CTRL_LEFT&&browser_tab_count>1){int ni=browser_tab_active-1;if(ni<0)ni=browser_tab_count-1;tab_select(ni);}if(pressed&SCE_CTRL_RIGHT&&browser_tab_count>1){int ni=(browser_tab_active+1)%browser_tab_count;tab_select(ni);}if(pressed&SCE_CTRL_CROSS){remote_click(cursor_x,cursor_y);refresh_frame();}if(pressed&SCE_CTRL_LTRIGGER){remote_simple("/back");refresh_frame();}if(pressed&SCE_CTRL_RTRIGGER){remote_simple("/forward");refresh_frame();}if(pressed&SCE_CTRL_SQUARE){open_search_keyboard(&mode,&return_mode,input,&keysel);}if(pressed&SCE_CTRL_TRIANGLE){if(search_text[0]){open_target(search_text);refresh_frame();}else open_search_keyboard(&mode,&return_mode,input,&keysel);}SceTouchData td;sceTouchPeek(SCE_TOUCH_PORT_FRONT,&td,1);browser_touch(&td,&mode,&return_mode,input,&keysel);if(++counter>=120){if(online){refresh_frame();spotify_refresh();network_probe();refresh_spotify_status();if(!net_proxy_ok)online=0;}else{online=create_session()==0;if(online){network_probe();refresh_frame();refresh_spotify_status();}}counter=0;}}}
  else if(mode==MODE_SETTINGS){if(pressed&SCE_CTRL_CIRCLE){if(settings_page){settings_page=0;settings_selected=0;settings_status[0]=0;}else mode=MODE_WEB;}else{int scount=settings_page==0?SETTINGS_CATEGORY_COUNT:(settings_page==5?CLEAR_COUNT:(settings_page==2?2:1));if(pressed&SCE_CTRL_UP&&settings_selected>0)settings_selected--;if(pressed&SCE_CTRL_DOWN&&settings_selected+1<scount)settings_selected++;if(pressed&SCE_CTRL_CROSS)settings_action();}}
  else {if(pressed&SCE_CTRL_START){mode=MODE_WEB;}else if(!sp.connected){if(pressed&SCE_CTRL_SELECT){spotify_login_web();mode=MODE_WEB;}}else if(search_view){if(pressed&SCE_CTRL_UP&&result_selected>0)result_selected--;if(pressed&SCE_CTRL_DOWN&&result_selected+1<result_count)result_selected++;if(pressed&SCE_CTRL_CROSS&&result_count){spotify_play_uri(proxy,results[result_selected].uri);search_view=0;spotify_refresh();}if(pressed&SCE_CTRL_SELECT&&result_count)spotify_queue_uri(proxy,results[result_selected].uri);if(pressed&SCE_CTRL_CIRCLE)search_view=0;if(pressed&SCE_CTRL_TRIANGLE){return_mode=MODE_SPOTIFY;mode=MODE_KEYBOARD;input[0]=0;keysel=0;}}else{if(pressed&SCE_CTRL_CROSS){spotify_command(proxy,sp.playing?"pause":"play");spotify_refresh();}if(pressed&SCE_CTRL_LTRIGGER){spotify_command(proxy,"previous");spotify_refresh();}if(pressed&SCE_CTRL_RTRIGGER){spotify_command(proxy,"next");spotify_refresh();}if(pressed&SCE_CTRL_LEFT){spotify_seek(proxy,sp.progress_ms-10000);spotify_refresh();}if(pressed&SCE_CTRL_RIGHT){spotify_seek(proxy,sp.progress_ms+10000);spotify_refresh();}if(pressed&SCE_CTRL_UP){spotify_volume(proxy,sp.volume+5);spotify_refresh();}if(pressed&SCE_CTRL_DOWN){spotify_volume(proxy,sp.volume-5);spotify_refresh();}if(pressed&SCE_CTRL_SQUARE)spotify_refresh();if(pressed&SCE_CTRL_TRIANGLE){return_mode=MODE_SPOTIFY;mode=MODE_KEYBOARD;input[0]=0;keysel=0;}if(pressed&SCE_CTRL_SELECT){spotify_login_web();mode=MODE_WEB;}SceTouchData td;sceTouchPeek(SCE_TOUCH_PORT_FRONT,&td,1);spotify_touch(&td);if(++counter>=180){spotify_refresh();counter=0;}}}
  vita2d_start_drawing();vita2d_clear_screen();if(mode==MODE_KEYBOARD){keyboard_draw(input,keysel,return_mode==MODE_SPOTIFY?"Spotify search":"Address / Google search");}else if(mode==MODE_SPOTIFY){draw_spotify();draw_spotify_touch_controls();}else if(mode==MODE_SETTINGS){draw_settings();}else if(frame){vita2d_draw_texture(frame,0,0);draw_browser_chrome();vita2d_draw_rectangle(cursor_x-6,cursor_y-1,13,3,RGBA8(20,255,120,255));vita2d_draw_rectangle(cursor_x-1,cursor_y-6,3,13,RGBA8(20,255,120,255));draw_mini_player();}else{draw_text(70,225,RGBA8(240,240,240,255),1.0f,"Proxy offline. Restart VitaSearch after starting proxy.");}vita2d_end_drawing();vita2d_swap_buffers();}
 if(cover)vita2d_free_texture(cover);if(frame)vita2d_free_texture(frame);if(font)vita2d_free_pgf(font);net_term();vita2d_fini();sceKernelExitProcess(0);return 0;}

#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
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
#define VITASEARCH_RELEASE "VitaSearch v0.99 RC60"

typedef enum { MODE_WEB=0, MODE_SPOTIFY=1, MODE_KEYBOARD=2, MODE_SETTINGS=3 } AppMode;
static AppMode mode=MODE_WEB;
static char proxy[256], api_key[128]="", ca_file[256]="", session[96]="";
static int cursor_x=SCREEN_W/2,cursor_y=SCREEN_H/2;
static float cursor_fx=SCREEN_W/2.0f,cursor_fy=SCREEN_H/2.0f;
static float cursor_vx=0.0f,cursor_vy=0.0f;
static vita2d_texture *frame=NULL,*cover=NULL;
static vita2d_pgf *font=NULL;
static SpotifyState sp;
static int spotify_control_selected=1; /* 0 previous, 1 play/pause, 2 next */
static SpotifyTrack results[RESULT_MAX];
static int result_count=0,result_selected=0,search_view=0;
static int settings_selected=0,javascript_enabled=1;
static int proxy_enabled=1;
static int javascript_pending=0;
static char spotify_notice[128]="";
static char settings_status[96]="";
static char last_cover[512]="";
static int online=0; /* browser session ready; kept separate from proxy/internet */
static int browser_session_ok=0;
static int net_proxy_ok=0;
static int net_internet_ok=0;
static int net_https_ok=0;
static int net_latency_ms=-1;
static int use_selected_search=1;
static int search_engine_index=0;
static VitaSpotifyStatus spotify_status;
static int spotify_status_rc=-1;

static unsigned int net_last_check=0;

static volatile int reconnect_busy=0;
static volatile int reconnect_done=0;
static volatile int reconnect_result=-1;
static volatile int reconnect_stage=0; /* 0 idle,1 health,2 fallback,3 session,4 frame */
static unsigned char *reconnect_frame_data=NULL;
static size_t reconnect_frame_size=0;

/* RC49 asynchronous browser navigation/login worker. */
static volatile int nav_busy=0;
static volatile int nav_done=0;
static volatile int nav_result=-1;
static volatile int nav_kind=0; /* 1 web/search, 2 Spotify login */
static char nav_target[512]="";
static unsigned char *nav_frame_data=NULL;
static size_t nav_frame_size=0;
static char nav_status[96]="";

static int browser_touch_down=0;
static unsigned int last_touch_click_us=0;
static int last_touch_x=-1000,last_touch_y=-1000;
static unsigned int last_x_click_us=0;
static int last_x_click_x=-1000,last_x_click_y=-1000;
static int pinch_active=0;
static int pinch_last_dist=0;
static int search_keyboard_open=0;
static int keyboard_purpose=0; /* 0 address/search, 1 spotify, 2 proxy URL, 3 API key, 4 focused web field */
static char search_text[512]="";
typedef struct { char title[48]; } BrowserTab;
#define TAB_MAX 6
static BrowserTab browser_tabs[TAB_MAX];
static int browser_tab_count=1;
static int browser_tab_active=0;
static int browser_tab_focus=0;
/* RC44 smooth held D-pad scrolling state. */
static int scroll_hold_dir=0;
static int scroll_hold_ticks=0;


static int refresh_frame(void);
static int network_probe(void);
static const char *reconnect_stage_text(void);

static const char *keys[]={"1","2","3","4","5","6","7","8","9","0","q","w","e","r","t","y","u","i","o","p","a","s","d","f","g","h","j","k","l",".","z","x","c","v","b","n","m","/","-","_",":","?","&","=","%","+","@","#","!","CAPS","SPACE","<","GO"};
#define KEY_COUNT ((int)(sizeof(keys)/sizeof(keys[0])))
#define KEY_COLS 10
static int keyboard_caps=0;
static int web_field_sensitive=0;
static int web_field_password=0;
static int web_field_secure_login=0;

static void draw_text(float x,float y,unsigned int c,float scale,const char*t){if(font)vita2d_pgf_draw_text(font,x,y,c,scale,t?t:"");}
static void json_escape(const char *src,char *dst,size_t cap){size_t j=0;for(size_t i=0;src[i]&&j+2<cap;i++){unsigned char c=src[i];if(c=='"'||c=='\\')dst[j++]='\\';if(c>=32)dst[j++]=c;}dst[j]=0;}
static int extract_session(const char*json){const char*p=strstr(json,"\"session\":\"");if(!p)return-1;p+=11;const char*e=strchr(p,'"');if(!e)return-1;size_t n=e-p;if(n>=sizeof(session))n=sizeof(session)-1;memcpy(session,p,n);session[n]=0;return 0;}
static int post_web(const char*path,const char*json){char url[512];snprintf(url,sizeof(url),"%s%s",proxy,path);NetBuffer b;int rc=net_post_json(url,json,&b);if(rc==0&&b.data)extract_session((char*)b.data);net_buffer_free(&b);return rc;}
static int json_get_string(const char *json,const char *key,char *out,size_t cap){
 if(!json||!key||!out||cap<1)return -1; char pat[96];snprintf(pat,sizeof(pat),"\"%s\":\"",key);const char*p=strstr(json,pat);if(!p)return-1;p+=strlen(pat);size_t j=0;while(*p&&j+1<cap){if(*p=='\"')break;if(*p=='\\'&&p[1]){p++;if(*p=='n')out[j++]='\n';else if(*p=='r')out[j++]='\r';else if(*p=='t')out[j++]='\t';else out[j++]=*p;p++;continue;}out[j++]=*p++;}out[j]=0;return 0;
}
static int remote_focus_click(int x,int y,char *value,size_t value_cap){
 char url[512],body[256];snprintf(url,sizeof(url),"%s/focus-click",proxy);snprintf(body,sizeof(body),"{\"session\":\"%s\",\"x\":%d,\"y\":%d}",session,x,y);NetBuffer b;memset(&b,0,sizeof(b));if(net_post_json(url,body,&b)!=0)return -1;int editable=0;web_field_sensitive=0;web_field_password=0;web_field_secure_login=0;if(b.data){extract_session((char*)b.data);editable=strstr((char*)b.data,"\"editable\":true")!=NULL;web_field_sensitive=strstr((char*)b.data,"\"sensitive\":true")!=NULL;web_field_password=strstr((char*)b.data,"\"password\":true")!=NULL;web_field_secure_login=strstr((char*)b.data,"\"secureLogin\":true")!=NULL;if(editable&&value&&value_cap&&!web_field_password)json_get_string((char*)b.data,"value",value,value_cap);}net_buffer_free(&b);return editable;
}
static int remote_set_focused_input_frame(const char *text){
 char url[512],esc[1536],body[1900];json_escape(text?text:"",esc,sizeof(esc));snprintf(url,sizeof(url),"%s/input-set-frame",proxy);snprintf(body,sizeof(body),"{\"session\":\"%s\",\"text\":\"%s\",\"submit\":true}",session,esc);NetBuffer b;memset(&b,0,sizeof(b));if(net_post_json(url,body,&b)!=0)return -1;vita2d_texture*t=vita2d_load_PNG_buffer(b.data);net_buffer_free(&b);if(!t)return -1;if(frame)vita2d_free_texture(frame);frame=t;return 0;
}
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

static int nav_worker(SceSize args, void *argp){
 (void)args;(void)argp;
 nav_result=-1;nav_frame_data=NULL;nav_frame_size=0;
 char url[512],body[1152];
 if(nav_kind==1){
   char e[768];json_escape(nav_target,e,sizeof(e));
   snprintf(url,sizeof(url),"%s/open-frame",proxy);
   snprintf(body,sizeof(body),"{\"session\":\"%s\",\"url\":\"%s\",\"search_engine\":\"%s\",\"use_selected_search\":%s}",session,e,search_engine_id(),use_selected_search?"true":"false");
 }else{
   snprintf(url,sizeof(url),"%s/spotify/session-login-frame",proxy);
   snprintf(body,sizeof(body),"{\"session\":\"%s\"}",session);
 }
 NetBuffer b;memset(&b,0,sizeof(b));
 if(net_post_json(url,body,&b)==0 && b.data && b.size){
   nav_frame_data=(unsigned char*)b.data;nav_frame_size=b.size;b.data=NULL;b.size=0;nav_result=0;
 }else nav_result=-1;
 net_buffer_free(&b);nav_done=1;nav_busy=0;sceKernelExitDeleteThread(0);return 0;
}

static int start_nav_worker(int kind,const char *target){
 if(nav_busy)return -1;
 nav_kind=kind;nav_done=0;nav_result=-1;nav_busy=1;nav_frame_data=NULL;nav_frame_size=0;
 if(target){strncpy(nav_target,target,sizeof(nav_target)-1);nav_target[sizeof(nav_target)-1]=0;}else nav_target[0]=0;
 snprintf(nav_status,sizeof(nav_status),kind==2?"Opening Spotify login...":"Loading...");
 SceUID th=sceKernelCreateThread("VitaSearchNav",nav_worker,0x10000100,0x12000,0,0,NULL);
 if(th<0){nav_busy=0;snprintf(nav_status,sizeof(nav_status),"Could not start navigation worker");return -1;}
 if(sceKernelStartThread(th,0,NULL)<0){nav_busy=0;sceKernelDeleteThread(th);snprintf(nav_status,sizeof(nav_status),"Could not start navigation worker");return -1;}
 return 0;
}

static void finish_nav_if_ready(void){
 if(!nav_done)return;nav_done=0;
 if(nav_result==0 && nav_frame_data && nav_frame_size){
   vita2d_texture *t=vita2d_load_PNG_buffer(nav_frame_data);
   free(nav_frame_data);nav_frame_data=NULL;nav_frame_size=0;
   if(t){if(frame)vita2d_free_texture(frame);frame=t;online=1;browser_session_ok=1;nav_status[0]=0;
     if(nav_kind==2){snprintf(spotify_notice,sizeof(spotify_notice),"Spotify login opened. Complete sign-in in browser.");mode=MODE_WEB;}
     return;}
 }
 if(nav_frame_data){free(nav_frame_data);nav_frame_data=NULL;nav_frame_size=0;}
 snprintf(nav_status,sizeof(nav_status),nav_kind==2?"Spotify login failed - check PC CMD":"Page load failed - press Triangle to retry");
 if(nav_kind==2)snprintf(spotify_notice,sizeof(spotify_notice),"Spotify login failed. Check PC CMD / Client ID.");
}
static int remote_simple(const char*p){char b[160];snprintf(b,sizeof(b),"{\"session\":\"%s\"}",session);return post_web(p,b);}
static int remote_click_count(int x,int y,int count){char b[288];snprintf(b,sizeof(b),"{\"session\":\"%s\",\"x\":%d,\"y\":%d,\"count\":%d}",session,x,y,count);return post_web("/click",b);}
static int remote_click(int x,int y){return remote_click_count(x,y,1);}
static int remote_zoom(int delta){char b[192];snprintf(b,sizeof(b),"{\"session\":\"%s\",\"delta\":%d}",session,delta);return post_web("/zoom",b);}
static int remote_scroll(int dx,int dy){char b[256];snprintf(b,sizeof(b),"{\"session\":\"%s\",\"dx\":%d,\"dy\":%d}",session,dx,dy);return post_web("/scroll",b);}

/* RC44: one round-trip scroll + screenshot. This avoids the old
   /scroll then /frame double request, which made D-pad scrolling feel jerky. */
static int remote_scroll_frame(int dy){
 char url[512],body[256];
 snprintf(url,sizeof(url),"%s/scroll-frame",proxy);
 snprintf(body,sizeof(body),"{\"session\":\"%s\",\"dy\":%d}",session,dy);
 NetBuffer b;
 if(net_post_json(url,body,&b)!=0)return -1;
 vita2d_texture *t=vita2d_load_PNG_buffer(b.data);
 net_buffer_free(&b);
 if(!t)return -1;
 if(frame)vita2d_free_texture(frame);
 frame=t;
 return 0;
}

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
static void append_keyboard_key(char *input,const char *key){
 if(!strcmp(key,"CAPS")){keyboard_caps=!keyboard_caps;return;}
 if(!strcmp(key,"SPACE")){append_input(input," ");return;}
 if(strlen(key)==1 && key[0]>='a' && key[0]<='z' && keyboard_caps){char t[2]={(char)(key[0]-'a'+'A'),0};append_input(input,t);return;}
 append_input(input,key);
}

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
 if(settings_status[0])draw_text(44,278,RGBA8(35,235,110,255),0.62f,settings_status);
 draw_text(44,330,RGBA8(135,150,145,255),0.60f,"X or Left/Right toggles JavaScript.");
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
 vita2d_draw_rectangle(34,64,892,52,RGBA8(35,235,110,settings_selected==0?255:110));
 draw_text(58,97,RGBA8(5,25,12,255),0.70f,reconnect_busy?reconnect_stage_text():"RECONNECT / REFRESH");
 draw_text(742,97,RGBA8(5,25,12,255),0.64f,"X / TAP");
 draw_settings_row(126,"Proxy",0,net_proxy_ok?"ONLINE":"OFFLINE");
 draw_settings_row(170,"Internet",0,net_internet_ok?"ONLINE":"OFFLINE");
 draw_settings_row(214,"Browser session",0,browser_session_ok?"READY":"NOT READY");
 draw_settings_row(258,"Spotify",0,(spotify_status_rc==0&&spotify_status.connected)?"CONNECTED":"NOT CONNECTED");
 draw_settings_row(302,"HTTPS",0,net_https_ok?"OK":"--");
 char tmp[96];snprintf(tmp,sizeof(tmp),"Latency: %d ms",net_latency_ms);
 draw_text(44,372,RGBA8(242,245,244,255),0.64f,tmp);
 draw_text(44,405,RGBA8(242,245,244,255),0.64f,"Proxy:");
 draw_text(104,405,RGBA8(35,235,110,255),0.60f,proxy);
 draw_text(44,440,RGBA8(135,150,145,255),0.57f,"Proxy, Internet, Browser and Spotify are tracked separately.");
 if(settings_status[0])draw_text(44,478,RGBA8(235,180,80,255),0.58f,settings_status);
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
 draw_settings_row(78,"Proxy",settings_selected==0,proxy_enabled?"ON":"OFF");
 draw_settings_row(126,"Edit Proxy URL",settings_selected==1,"X");
 draw_settings_row(174,"Edit API Key",settings_selected==2,api_key[0]?"SET":"NOT SET");
 draw_settings_row(222,"Proxy status",0,(proxy_enabled&&net_proxy_ok)?"ONLINE":"OFFLINE");
 draw_text(44,282,RGBA8(242,245,244,255),0.68f,"Proxy address:");
 draw_text(44,315,RGBA8(35,235,110,255),0.64f,proxy);
 draw_text(44,356,RGBA8(165,180,175,255),0.62f,"API key must match the PC proxy exactly.");
 draw_text(44,392,RGBA8(165,180,175,255),0.62f,"Use lowercase key for the Vita keyboard.");
 draw_text(44,428,RGBA8(135,150,145,255),0.60f,"Up/Down choose, X edit. Then Network -> Reconnect.");
 if(settings_status[0])draw_text(44,472,RGBA8(35,235,110,255),0.62f,settings_status);
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
  "X        Select / double-click",
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
 draw_text(48,444,RGBA8(242,245,244,255),0.68f,"Tap/double-tap, pinch zoom, tabs and Spotify");
}

static void draw_settings_appearance(void){
 draw_settings_header("Appearance");
 draw_settings_row(78,"Theme",0,"DARK");
 draw_settings_row(126,"Accent",0,"NEON GREEN");
 draw_text(44,196,RGBA8(165,180,175,255),0.64f,"Designed for the PS Vita 960 x 544 display.");
}

static void draw_settings_about(void){
 draw_settings_header("About VitaSearch");
 draw_text(44,92,RGBA8(35,235,110,255),0.90f,VITASEARCH_RELEASE);
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


static void open_settings_root(void){
 mode=MODE_SETTINGS;
 settings_page=0;
 settings_selected=0;
 settings_status[0]=0;
}

static int make_http_8080_fallback(const char *src,char *dst,size_t cap){
 if(!src||strncmp(src,"https://",8)!=0)return -1;
 const char *host=src+8;
 const char *colon=strrchr(host,':');
 if(!colon)return -1;
 if(strcmp(colon,":8443")!=0)return -1;
 size_t hostlen=(size_t)(colon-host);
 if(hostlen+16>=cap)return -1;
 snprintf(dst,cap,"http://%.*s:8080",(int)hostlen,host);
 return 0;
}

static int reconnect_worker(SceSize args, void *argp){
 (void)args;(void)argp;
 reconnect_result=-1;
 reconnect_frame_data=NULL;
 reconnect_frame_size=0;
 reconnect_stage=1;

 /* First try exactly what Settings shows. */
 int health_rc=network_probe();

 /* Common RC34 configuration mismatch:
    Vita = https://PC:8443, default proxy = http://PC:8080.
    Try the matching HTTP endpoint automatically. */
 if(health_rc!=0 || !net_proxy_ok){
   char fallback[256];
   if(make_http_8080_fallback(proxy,fallback,sizeof(fallback))==0){
     reconnect_stage=2;
     char original[256];
     strncpy(original,proxy,sizeof(original)-1);original[sizeof(original)-1]=0;
     strncpy(proxy,fallback,sizeof(proxy)-1);proxy[sizeof(proxy)-1]=0;
     health_rc=network_probe();
     if(health_rc!=0 || !net_proxy_ok){
       strncpy(proxy,original,sizeof(proxy)-1);proxy[sizeof(proxy)-1]=0;
     }
   }
 }

 if(health_rc!=0 || !net_proxy_ok){
   reconnect_result=-2;
   reconnect_stage=0;
   reconnect_done=1;
   reconnect_busy=0;
   sceKernelExitDeleteThread(0);
   return 0;
 }

 reconnect_stage=3;
 if(create_session()!=0){
   browser_session_ok=0;
   online=0;
   reconnect_result=-3;
   reconnect_stage=0;
   reconnect_done=1;
   reconnect_busy=0;
   sceKernelExitDeleteThread(0);
   return 0;
 }

 if(javascript_pending){
   if(settings_set_javascript(proxy,session,javascript_enabled)==0)
     javascript_pending=0;
 }

 browser_session_ok=1;
 online=1;
 reconnect_stage=4;
 char u[512];
 snprintf(u,sizeof(u),"%s/frame?session=%s",proxy,session);
 NetBuffer b;
 memset(&b,0,sizeof(b));
 if(net_get(u,&b)!=0 || !b.data || b.size==0){
   net_buffer_free(&b);
   reconnect_result=-4;
   reconnect_stage=0;
   reconnect_done=1;
   reconnect_busy=0;
   sceKernelExitDeleteThread(0);
   return 0;
 }

 reconnect_frame_data=(unsigned char*)b.data;
 reconnect_frame_size=b.size;
 b.data=NULL;b.size=0;
 reconnect_result=0;
 reconnect_stage=0;
 reconnect_done=1;
 reconnect_busy=0;
 sceKernelExitDeleteThread(0);
 return 0;
}

static void reconnect_refresh(void){
 if(!proxy_enabled){
   snprintf(settings_status,sizeof(settings_status),"Proxy is OFF");
   online=0;browser_session_ok=0;net_proxy_ok=0;net_internet_ok=0;
   return;
 }
 if(reconnect_busy){
   snprintf(settings_status,sizeof(settings_status),"Reconnect already running...");
   return;
 }
 reconnect_done=0;
 reconnect_result=-1;
 reconnect_stage=1;
 reconnect_busy=1;
 snprintf(settings_status,sizeof(settings_status),"Checking proxy in background...");
 SceUID th=sceKernelCreateThread("VitaSearchReconnect",reconnect_worker,0x10000100,0x10000,0,0,NULL);
 if(th<0){
   reconnect_busy=0;
   snprintf(settings_status,sizeof(settings_status),"Could not start reconnect worker");
   return;
 }
 if(sceKernelStartThread(th,0,NULL)<0){
   reconnect_busy=0;
   sceKernelDeleteThread(th);
   snprintf(settings_status,sizeof(settings_status),"Could not start reconnect worker");
 }
}

static const char *reconnect_stage_text(void){
 if(reconnect_stage==1)return "Checking configured proxy...";
 if(reconnect_stage==2)return "Trying HTTP port 8080...";
 if(reconnect_stage==3)return "Creating browser session...";
 if(reconnect_stage==4)return "Downloading first frame...";
 return reconnect_busy?"Connecting...":"Proxy offline";
}

static void finish_reconnect_if_ready(void){
 if(!reconnect_done)return;
 reconnect_done=0;

 if(reconnect_result==0 && reconnect_frame_data && reconnect_frame_size){
   vita2d_texture *t=vita2d_load_PNG_buffer(reconnect_frame_data);
   free(reconnect_frame_data);
   reconnect_frame_data=NULL;
   reconnect_frame_size=0;
   if(t){
     if(frame)vita2d_free_texture(frame);
     frame=t;
     browser_session_ok=1;
     online=1;
     snprintf(settings_status,sizeof(settings_status),
              net_internet_ok?"Proxy + Internet ONLINE":"Proxy ONLINE, Internet unavailable");
     reconnect_stage=0;
     mode=MODE_WEB;
     return;
   }
   snprintf(settings_status,sizeof(settings_status),"Browser session READY, frame decode failed");
   return;
 }

 if(reconnect_frame_data){free(reconnect_frame_data);reconnect_frame_data=NULL;reconnect_frame_size=0;}
 if(reconnect_result==-2){browser_session_ok=0;online=0;snprintf(settings_status,sizeof(settings_status),"Proxy unreachable - check PC IP/port/firewall");}
 else if(reconnect_result==-3){browser_session_ok=0;online=0;snprintf(settings_status,sizeof(settings_status),"Proxy ONLINE, browser session failed");}
 else if(reconnect_result==-4){browser_session_ok=1;online=1;snprintf(settings_status,sizeof(settings_status),"Proxy + session OK, frame request failed");}
 else snprintf(settings_status,sizeof(settings_status),"Reconnect failed");
}

static void settings_action(void){
 settings_status[0]=0;
 if(settings_page==0){settings_page=settings_selected+1;settings_selected=0;return;}
 if(settings_page==1){
   int next=!javascript_enabled;
   javascript_enabled=next;
   javascript_pending=1;
   if(proxy_enabled&&online&&settings_set_javascript(proxy,session,next)==0){
     javascript_pending=0;
     refresh_frame();
     snprintf(settings_status,sizeof(settings_status),"JavaScript %s",next?"ON":"OFF");
   }else{
     snprintf(settings_status,sizeof(settings_status),"JavaScript %s (apply on reconnect)",next?"ON":"OFF");
   }
   return;
 }
 if(settings_page==2){
   if(settings_selected==0)use_selected_search=!use_selected_search;
   else search_engine_index=(search_engine_index+1)%3;
   return;
 }
 if(settings_page==3){reconnect_refresh();return; }
 if(settings_page==6){
   if(settings_selected==0){
     proxy_enabled=!proxy_enabled;
     if(!proxy_enabled){
       online=0;browser_session_ok=0;net_proxy_ok=0;net_internet_ok=0;net_latency_ms=-1;
       snprintf(settings_status,sizeof(settings_status),"Proxy OFF");
     }else snprintf(settings_status,sizeof(settings_status),"Proxy ON - use Network Reconnect");
   }
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
static int spotify_login_web(void){
 if(!proxy_enabled){snprintf(spotify_notice,sizeof(spotify_notice),"Proxy is OFF. Enable it in Settings.");return -1;}
 if(!browser_session_ok){snprintf(spotify_notice,sizeof(spotify_notice),"Browser session not ready. Network -> Reconnect.");return -1;}
 if(start_nav_worker(2,NULL)!=0){snprintf(spotify_notice,sizeof(spotify_notice),"Spotify login already starting...");return -1;}
 snprintf(spotify_notice,sizeof(spotify_notice),"Opening Spotify login...");
 return 0;
}

static void keyboard_draw(const char*input,int sel,const char*title){
 vita2d_draw_rectangle(0,0,960,544,RGBA8(10,12,16,255));
 draw_text(30,30,RGBA8(40,240,120,255),1.0f,title);
 vita2d_draw_rectangle(28,46,904,58,RGBA8(25,30,38,255));
 draw_text(42,84,RGBA8(235,245,240,255),1.0f,input[0]?input:"Type...");
 for(int i=0;i<KEY_COUNT;i++){
  int r=i/KEY_COLS,c=i%KEY_COLS;float x=30+c*90,y=126+r*64,w=78;
  unsigned int bg=i==sel?RGBA8(30,230,110,255):RGBA8(38,44,54,255);
  if(!strcmp(keys[i],"CAPS")&&keyboard_caps)bg=RGBA8(30,230,110,255);
  vita2d_draw_rectangle(x,y,w,49,bg);
  const char *label=keys[i];char upper[2]={0,0};
  if(strlen(label)==1&&label[0]>='a'&&label[0]<='z'&&keyboard_caps){upper[0]=(char)(label[0]-'a'+'A');label=upper;}
  draw_text(x+(!strcmp(keys[i],"SPACE")?9:(!strcmp(keys[i],"CAPS")?12:22)),y+33,(i==sel||(!strcmp(keys[i],"CAPS")&&keyboard_caps))?RGBA8(0,20,10,255):RGBA8(240,240,240,255),!strcmp(keys[i],"SPACE")?0.58f:(!strcmp(keys[i],"CAPS")?0.62f:0.9f),label);
 }
 draw_text(30,527,RGBA8(170,180,190,255),0.68f,keyboard_caps?"CAPS ON   D-pad move   X type/select   O close   Triangle GO":"CAPS off   D-pad move   X type/select   O close   Triangle GO");
}

static void draw_progress(int y){float pct=sp.duration_ms>0?(float)sp.progress_ms/sp.duration_ms:0;if(pct<0)pct=0;if(pct>1)pct=1;vita2d_draw_rectangle(250,y,650,8,RGBA8(45,55,62,255));vita2d_draw_rectangle(250,y,650*pct,8,RGBA8(35,235,110,255));}
static void draw_mini_player(void){vita2d_draw_rectangle(0,500,960,44,RGBA8(11,15,18,245));draw_text(18,528,RGBA8(245,245,245,255),0.66f,sp.title[0]?sp.title:"Spotify");draw_text(690,528,RGBA8(35,235,110,255),0.66f,sp.playing?"X Pause":"X Play");draw_text(800,528,RGBA8(190,200,195,255),0.54f,"START Spotify");}

static void draw_spotify(void){vita2d_draw_rectangle(0,0,960,544,RGBA8(7,10,9,255));draw_text(28,38,RGBA8(35,235,110,255),1.25f,"VitaSearch Spotify");draw_text(735,36,RGBA8(160,175,165,255),0.65f,"START: Web");
 if(!sp.connected){draw_text(70,180,RGBA8(245,245,245,255),1.15f,"Spotify is not connected");draw_text(70,225,RGBA8(170,185,175,255),0.8f,"Start the proxy, then press X to connect Spotify.");vita2d_draw_rectangle(320,285,320,58,RGBA8(35,235,110,255));draw_text(400,322,RGBA8(5,25,12,255),0.82f,nav_busy?"CONNECTING...":"X  CONNECT");draw_text(70,390,RGBA8(150,165,160,255),0.68f,"START: Web    O: Back    SELECT: Settings");if(spotify_notice[0])draw_text(70,432,RGBA8(235,180,80,255),0.66f,spotify_notice);return;}
 if(search_view){draw_text(30,78,RGBA8(220,230,225,255),0.78f,"SEARCH RESULTS    Triangle: new search    O: Now Playing    SELECT: add to queue");for(int i=0;i<result_count;i++){int y=103+i*45;vita2d_draw_rectangle(24,y,912,39,i==result_selected?RGBA8(30,75,48,255):RGBA8(20,25,23,255));draw_text(38,y+25,RGBA8(245,245,245,255),0.74f,results[i].name);draw_text(520,y+25,RGBA8(150,170,158,255),0.62f,results[i].artist);}if(!result_count)draw_text(35,150,RGBA8(180,190,185,255),0.8f,"No results. Press Triangle to search.");draw_mini_player();return;}
 if(cover)vita2d_draw_texture_scale(cover,40,88,0.72f,0.72f);else vita2d_draw_rectangle(40,88,240,240,RGBA8(25,32,28,255));draw_text(320,110,RGBA8(150,170,158,255),0.65f,"NOW PLAYING");draw_text(320,154,RGBA8(245,245,245,255),1.12f,sp.title[0]?sp.title:"No active playback");draw_text(320,188,RGBA8(175,190,180,255),0.82f,sp.artist);draw_text(320,222,RGBA8(135,155,143,255),0.68f,sp.device);
 draw_progress(260);char info[128];snprintf(info,sizeof(info),"D-pad Left/Right: choose control     Volume: %d%% (Up/Down)",sp.volume);draw_text(320,294,RGBA8(175,190,180,255),0.68f,info);
 vita2d_draw_rectangle(320,330,150,54,spotify_control_selected==0?RGBA8(35,235,110,255):RGBA8(28,35,31,255));vita2d_draw_rectangle(485,330,150,54,spotify_control_selected==1?RGBA8(35,235,110,255):RGBA8(28,35,31,255));vita2d_draw_rectangle(650,330,150,54,spotify_control_selected==2?RGBA8(35,235,110,255):RGBA8(28,35,31,255));draw_text(352,364,spotify_control_selected==0?RGBA8(5,25,12,255):RGBA8(235,240,238,255),0.78f,"Previous");draw_text(515,364,spotify_control_selected==1?RGBA8(5,25,12,255):RGBA8(235,240,238,255),0.78f,sp.playing?"Pause":"Play");draw_text(684,364,spotify_control_selected==2?RGBA8(5,25,12,255):RGBA8(235,240,238,255),0.78f,"Next");draw_text(320,426,RGBA8(35,235,110,255),0.75f,"Triangle: Search     Square: Refresh     SELECT: Login/device page");draw_mini_player();}



static void open_search_keyboard(AppMode *mode,AppMode *return_mode,char *input,int *keysel){
 *return_mode=MODE_WEB;
 *mode=MODE_KEYBOARD;
 *keysel=0;
 keyboard_caps=0;
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
 if(input[0])start_nav_worker(1,input);
 search_keyboard_open=0;
 *mode=return_mode;
}

static void open_web_field_keyboard(AppMode *mode,AppMode *return_mode,char *input,int *keysel,const char *value){
 if(web_field_sensitive&&!web_field_secure_login){snprintf(nav_status,sizeof(nav_status),"Secure login: use HTTPS proxy + HTTPS website");return;}
 *return_mode=MODE_WEB;*mode=MODE_KEYBOARD;*keysel=0;keyboard_caps=0;keyboard_purpose=4;search_keyboard_open=1;if(web_field_password)input[0]=0;else{strncpy(input,value?value:"",INPUT_MAX);input[INPUT_MAX]=0;}
}
static void submit_web_field_keyboard(AppMode *mode,AppMode return_mode,char *input){
 if(remote_set_focused_input_frame(input)!=0)snprintf(nav_status,sizeof(nav_status),web_field_sensitive?"Secure login blocked/failed - HTTPS required":"Could not type into web field");keyboard_purpose=0;search_keyboard_open=0;*mode=return_mode;
}
static void open_proxy_keyboard(AppMode *return_mode,char *input,int *keysel){
 *return_mode=MODE_SETTINGS;
 mode=MODE_KEYBOARD;
 *keysel=0;
 keyboard_caps=0;
 keyboard_purpose=2;
 search_keyboard_open=1;
 strncpy(input,proxy,INPUT_MAX);
 input[INPUT_MAX]=0;
}

static void submit_proxy_keyboard(AppMode return_mode,char *input){
 if(input[0]){
   if(strncmp(input,"http://",7)!=0 && strncmp(input,"https://",8)!=0){
     snprintf(settings_status,sizeof(settings_status),"Invalid proxy URL. Must start with http:// or https://");
   }else{
     strncpy(proxy,input,sizeof(proxy)-1);
     proxy[sizeof(proxy)-1]=0;
     config_save_proxy(proxy,api_key,ca_file);
     online=0;browser_session_ok=0;net_proxy_ok=0;net_internet_ok=0;net_latency_ms=-1;
     snprintf(settings_status,sizeof(settings_status),"Proxy URL saved. Network -> Reconnect.");
   }
 }
 keyboard_purpose=0;
 search_keyboard_open=0;
 mode=return_mode;
}

static void open_api_key_keyboard(AppMode *return_mode,char *input,int *keysel){
 *return_mode=MODE_SETTINGS;
 mode=MODE_KEYBOARD;
 *keysel=0;
 keyboard_caps=0;
 keyboard_purpose=3;
 search_keyboard_open=1;
 strncpy(input,api_key,INPUT_MAX);
 input[INPUT_MAX]=0;
}

static void submit_api_key_keyboard(AppMode return_mode,char *input){
 if(input[0]){
   strncpy(api_key,input,sizeof(api_key)-1);
   api_key[sizeof(api_key)-1]=0;
   net_set_api_key(api_key);
   config_save_proxy(proxy,api_key,ca_file);
   online=0;browser_session_ok=0;
   snprintf(settings_status,sizeof(settings_status),"API key saved. Network -> Reconnect.");
 }
 keyboard_purpose=0;
 search_keyboard_open=0;
 mode=return_mode;
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
   if(net_proxy_ok&&browser_session_ok&&net_internet_ok){snprintf(ns,sizeof(ns),"Online %dms",net_latency_ms);nc=RGBA8(35,235,110,255);}
   else if(net_proxy_ok&&browser_session_ok){snprintf(ns,sizeof(ns),"Proxy+Session");nc=RGBA8(235,180,80,255);}
   else if(net_proxy_ok){snprintf(ns,sizeof(ns),"Proxy online / No session");nc=RGBA8(235,180,80,255);}
   else snprintf(ns,sizeof(ns),"Proxy offline");
   draw_text(790,64,nc,0.54f,ns);
 }
 if(nav_busy){vita2d_draw_rectangle(330,92,300,38,RGBA8(8,18,14,225));draw_text(390,118,RGBA8(35,235,110,255),0.68f,nav_status[0]?nav_status:"Loading...");}
 else if(nav_status[0]){vita2d_draw_rectangle(270,92,420,38,RGBA8(28,20,12,225));draw_text(300,118,RGBA8(235,180,80,255),0.62f,nav_status);}
}

static void browser_touch(const SceTouchData *td, AppMode *mode, AppMode *return_mode, char *input, int *keysel){
 int down=td&&td->reportNum>0;

 /* RC47 two-finger pinch: spread apart = zoom in, pinch together = zoom out. */
 if(td&&td->reportNum>=2){
   int x0=td->report[0].x*960/1920, y0=td->report[0].y*544/1088;
   int x1=td->report[1].x*960/1920, y1=td->report[1].y*544/1088;
   int dx=x1-x0,dy=y1-y0;
   int dist=dx*dx+dy*dy; /* squared distance avoids sqrt/libm */
   if(!pinch_active){pinch_active=1;pinch_last_dist=dist;}
   else{
     int delta=dist-pinch_last_dist;
     if(delta>7000){remote_zoom(+10);pinch_last_dist=dist;refresh_frame();}
     else if(delta<-7000){remote_zoom(-10);pinch_last_dist=dist;refresh_frame();}
   }
   browser_touch_down=1;
   return;
 }
 if(pinch_active){pinch_active=0;pinch_last_dist=0;browser_touch_down=down;return;}

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
   else if(y>=34&&y<=82){
     open_search_keyboard(mode,return_mode,input,keysel);
   } else if(y<500){
     cursor_x=x; cursor_y=y;
     cursor_fx=(float)x; cursor_fy=(float)y;
     cursor_vx=cursor_vy=0.0f;

     /* RC54: editable HTML fields open VitaSearch keyboard; other elements are clicked normally by the proxy. */
     char field_value[INPUT_MAX+1]={0};
     int editable=remote_focus_click(x,y,field_value,sizeof(field_value));
     if(editable>0){open_web_field_keyboard(mode,return_mode,input,keysel,field_value);}
     else{last_touch_click_us=sceKernelGetProcessTimeLow();last_touch_x=x;last_touch_y=y;refresh_frame();}
   }
 }
 browser_touch_down=down;
}


static int keyboard_touch_down=0;
static void keyboard_touch_input(const SceTouchData *td,AppMode *mode_ptr,AppMode return_mode,char *input,int *keysel){
 int down=td&&td->reportNum>0;
 if(down&&!keyboard_touch_down){
   int x=td->report[0].x*960/1920;
   int y=td->report[0].y*544/1088;
   if(y>=126 && y<=510 && x>=30 && x<=930){
     int row=(y-126)/64;
     int col=(x-30)/90;
     int idx=row*KEY_COLS+col;
     if(idx>=0 && idx<KEY_COUNT){
       *keysel=idx;
       if(idx==KEY_COUNT-1){
         if(keyboard_purpose==2)submit_proxy_keyboard(return_mode,input);
         else if(keyboard_purpose==3)submit_api_key_keyboard(return_mode,input);
         else if(keyboard_purpose==4)submit_web_field_keyboard(mode_ptr,return_mode,input);
         else if(return_mode==MODE_WEB)submit_search_keyboard(mode_ptr,return_mode,input);
         else{
           if(input[0]){result_count=spotify_search(proxy,input,results,RESULT_MAX);result_selected=0;search_view=1;spotify_refresh();}
           *mode_ptr=return_mode;
         }
       }else if(!strcmp(keys[idx],"<")){
         size_t n=strlen(input);if(n)input[n-1]=0;
       }else append_keyboard_key(input,keys[idx]);
     }
   }
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
   if(x>=250&&x<390){spotify_command(proxy,"previous");snprintf(spotify_notice,sizeof(spotify_notice),"Previous sent");}
   else if(x>=390&&x<570){spotify_command(proxy,"toggle");snprintf(spotify_notice,sizeof(spotify_notice),"Play/Pause sent");}
   else if(x>=570&&x<=710){spotify_command(proxy,"next");snprintf(spotify_notice,sizeof(spotify_notice),"Next sent");}
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

int main(void){sceSysmoduleLoadModule(SCE_SYSMODULE_NET);sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);vita2d_init();font=vita2d_load_default_pgf();sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,SCE_TOUCH_SAMPLING_STATE_START);config_load(proxy,sizeof(proxy),api_key,sizeof(api_key),ca_file,sizeof(ca_file));net_init();net_set_api_key(api_key);net_set_ca_file(ca_file);network_probe();browser_session_ok=(create_session()==0);online=browser_session_ok;
 memset(&spotify_status,0,sizeof(spotify_status));
 snprintf(spotify_status.callback,sizeof(spotify_status.callback),"unknown");
 snprintf(spotify_status.token,sizeof(spotify_status.token),"unknown");
 snprintf(spotify_status.device,sizeof(spotify_status.device),"None");
 if(online){network_probe();refresh_spotify_status();}
 snprintf(browser_tabs[0].title,sizeof(browser_tabs[0].title),"Start");if(online){refresh_frame();}
 AppMode return_mode=MODE_WEB;unsigned int old=0;int counter=0,keysel=0;char input[INPUT_MAX+1]="";
 for(;;){finish_reconnect_if_ready();finish_nav_if_ready();SceCtrlData pad;memset(&pad,0,sizeof(pad));int pad_count=sceCtrlPeekBufferPositive(0,&pad,1);if(pad_count<1){pad.buttons=old;pad.lx=128;pad.ly=128;}unsigned int pressed=pad.buttons&~old;old=pad.buttons;if((pad.buttons&SCE_CTRL_START)&&(pad.buttons&SCE_CTRL_SELECT))break;
  if(mode==MODE_KEYBOARD){int row=keysel/KEY_COLS,col=keysel%KEY_COLS;if(pressed&SCE_CTRL_LEFT)col=(col+9)%10;if(pressed&SCE_CTRL_RIGHT)col=(col+1)%10;if(pressed&SCE_CTRL_UP)row=row>0?row-1:5;if(pressed&SCE_CTRL_DOWN)row=row<5?row+1:0;keysel=row*10+col;if(keysel>=KEY_COUNT)keysel=KEY_COUNT-1;if(pressed&SCE_CTRL_CIRCLE){if(keyboard_purpose==2||keyboard_purpose==3||keyboard_purpose==4){keyboard_purpose=0;search_keyboard_open=0;mode=return_mode;}else if(return_mode==MODE_WEB)close_search_keyboard(&mode,return_mode,input);else mode=return_mode;}else if(pressed&SCE_CTRL_TRIANGLE){if(keyboard_purpose==2)submit_proxy_keyboard(return_mode,input);else if(keyboard_purpose==3)submit_api_key_keyboard(return_mode,input);else if(keyboard_purpose==4)submit_web_field_keyboard(&mode,return_mode,input);else if(return_mode==MODE_WEB)submit_search_keyboard(&mode,return_mode,input);else{if(input[0]){result_count=spotify_search(proxy,input,results,RESULT_MAX);result_selected=0;search_view=1;spotify_refresh();}mode=return_mode;}}else if(pressed&SCE_CTRL_CROSS){if(keysel==KEY_COUNT-1){if(keyboard_purpose==2)submit_proxy_keyboard(return_mode,input);else if(keyboard_purpose==3)submit_api_key_keyboard(return_mode,input);else if(keyboard_purpose==4)submit_web_field_keyboard(&mode,return_mode,input);else if(return_mode==MODE_WEB)submit_search_keyboard(&mode,return_mode,input);else{if(input[0]){result_count=spotify_search(proxy,input,results,RESULT_MAX);result_selected=0;search_view=1;spotify_refresh();}mode=return_mode;}}else if(!strcmp(keys[keysel],"<")){size_t n=strlen(input);if(n)input[n-1]=0;}else append_keyboard_key(input,keys[keysel]);}if(mode==MODE_KEYBOARD){SceTouchData ktd;sceTouchPeek(SCE_TOUCH_PORT_FRONT,&ktd,1);keyboard_touch_input(&ktd,&mode,return_mode,input,&keysel);}}
  else if(mode==MODE_WEB){if(pressed&SCE_CTRL_START){mode=MODE_SPOTIFY;counter=0;}else if(pressed&SCE_CTRL_SELECT){mode=MODE_SETTINGS;settings_page=0;settings_selected=0;settings_status[0]=0;}else if(!online){
 if(pressed&SCE_CTRL_SELECT){open_settings_root();}
 else if(pressed&SCE_CTRL_CROSS||pressed&SCE_CTRL_TRIANGLE){reconnect_refresh();}
 SceTouchData otd;memset(&otd,0,sizeof(otd));sceTouchPeek(SCE_TOUCH_PORT_FRONT,&otd,1);
 static int offline_touch_down=0;int offline_touch_now=otd.reportNum>0;
 if(offline_touch_now&&!offline_touch_down){
   int tx=otd.report[0].x*960/1919;int ty=otd.report[0].y*544/1087;
   if(tx>=250&&tx<=710&&ty>=245&&ty<=325)reconnect_refresh();
   else if(tx>=250&&tx<=710&&ty>=345&&ty<=420)open_settings_root();
 }
 offline_touch_down=offline_touch_now;
 /* RC39: restore authenticated /session creation; statuses remain independent. */
}else if(online){
 /* RC47: standard controller sampling reliably reports the built-in L-stick.
    Lower deadzone + responsive filtering keeps movement smooth without feeling stuck. */
 int ax=(int)pad.lx-128,ay=(int)pad.ly-128;
 const int dead=12;
 float tx=0.0f,ty=0.0f;
 if(ax>dead||ax<-dead){float n=(float)(ax>0?ax-dead:ax+dead)/(128.0f-dead);tx=n*(2.0f+7.5f*(n<0?-n:n));}
 if(ay>dead||ay<-dead){float n=(float)(ay>0?ay-dead:ay+dead)/(128.0f-dead);ty=n*(2.0f+7.5f*(n<0?-n:n));}
 cursor_vx=cursor_vx*0.55f+tx*0.45f;
 cursor_vy=cursor_vy*0.55f+ty*0.45f;
 if(tx==0.0f&&cursor_vx<0.12f&&cursor_vx>-0.12f)cursor_vx=0.0f;
 if(ty==0.0f&&cursor_vy<0.12f&&cursor_vy>-0.12f)cursor_vy=0.0f;
 cursor_fx+=cursor_vx; cursor_fy+=cursor_vy;
 if(cursor_fx<0)cursor_fx=0;if(cursor_fx>959)cursor_fx=959;
 if(cursor_fy<82)cursor_fy=82;if(cursor_fy>499)cursor_fy=499;
 cursor_x=(int)(cursor_fx+0.5f);cursor_y=(int)(cursor_fy+0.5f);/* RC44: Up/Down scroll while HELD, with smaller accelerated steps and a single scroll+frame request. */int scroll_dir=0;if(pad.buttons&SCE_CTRL_UP)scroll_dir=-1;else if(pad.buttons&SCE_CTRL_DOWN)scroll_dir=1;if(scroll_dir){if(scroll_hold_dir!=scroll_dir){scroll_hold_dir=scroll_dir;scroll_hold_ticks=0;}scroll_hold_ticks++;int step=(scroll_hold_ticks<7)?46:((scroll_hold_ticks<18)?70:100);remote_scroll_frame(scroll_dir*step);}else{scroll_hold_dir=0;scroll_hold_ticks=0;}if(pressed&SCE_CTRL_LEFT&&browser_tab_count>1){int ni=browser_tab_active-1;if(ni<0)ni=browser_tab_count-1;tab_select(ni);}if(pressed&SCE_CTRL_RIGHT&&browser_tab_count>1){int ni=(browser_tab_active+1)%browser_tab_count;tab_select(ni);}if(pressed&SCE_CTRL_CROSS){char field_value[INPUT_MAX+1]={0};int editable=remote_focus_click(cursor_x,cursor_y,field_value,sizeof(field_value));if(editable>0){open_web_field_keyboard(&mode,&return_mode,input,&keysel,field_value);}else{last_x_click_us=sceKernelGetProcessTimeLow();last_x_click_x=cursor_x;last_x_click_y=cursor_y;refresh_frame();}}if(pressed&SCE_CTRL_LTRIGGER){remote_simple("/back");refresh_frame();}if(pressed&SCE_CTRL_RTRIGGER){remote_simple("/forward");refresh_frame();}if(pressed&SCE_CTRL_SQUARE){open_search_keyboard(&mode,&return_mode,input,&keysel);}if(pressed&SCE_CTRL_TRIANGLE){if(search_text[0])start_nav_worker(1,search_text);else open_search_keyboard(&mode,&return_mode,input,&keysel);}SceTouchData td;sceTouchPeek(SCE_TOUCH_PORT_FRONT,&td,1);browser_touch(&td,&mode,&return_mode,input,&keysel);/* RC54: Never perform periodic network I/O in the WEB input/render loop.
   refresh_frame/network_probe/Spotify HTTP can block for hundreds of ms or seconds,
   which made the local L-stick cursor appear to freeze. Frames are refreshed only
   after explicit browser actions/navigation workers; status refresh happens outside
   continuous pointer sampling. */
if(!nav_busy){counter++; if(counter>3600) counter=0;}}}
  else if(mode==MODE_SETTINGS){if(pressed&SCE_CTRL_CIRCLE){if(settings_page){settings_page=0;settings_selected=0;settings_status[0]=0;}else mode=MODE_WEB;}else{int scount=settings_page==0?SETTINGS_CATEGORY_COUNT:(settings_page==5?CLEAR_COUNT:(settings_page==6?3:(settings_page==2?2:1)));if(pressed&SCE_CTRL_UP&&settings_selected>0)settings_selected--;if(pressed&SCE_CTRL_DOWN&&settings_selected+1<scount)settings_selected++;if(pressed&SCE_CTRL_CROSS){if(settings_page==6&&settings_selected==1)open_proxy_keyboard(&return_mode,input,&keysel);else if(settings_page==6&&settings_selected==2)open_api_key_keyboard(&return_mode,input,&keysel);else settings_action();}if((pressed&SCE_CTRL_TRIANGLE)&&settings_page==3)reconnect_refresh();if((pressed&SCE_CTRL_LEFT||pressed&SCE_CTRL_RIGHT)&&(settings_page==1||settings_page==2||(settings_page==6&&settings_selected==0)))settings_action();
SceTouchData std;memset(&std,0,sizeof(std));sceTouchPeek(SCE_TOUCH_PORT_FRONT,&std,1);
static int settings_touch_down=0;
int touch_now=std.reportNum>0;
if(settings_page==3&&touch_now&&!settings_touch_down){
 int tx=std.report[0].x*960/1919;
 int ty=std.report[0].y*544/1087;
 if(tx>=34&&tx<=926&&ty>=72&&ty<=130)reconnect_refresh();
}
settings_touch_down=touch_now;}}
  else {if(pressed&SCE_CTRL_START){mode=MODE_WEB;}else if(!sp.connected){if(pressed&SCE_CTRL_CIRCLE){mode=MODE_WEB;}else if(pressed&SCE_CTRL_SELECT){mode=MODE_SETTINGS;settings_page=0;settings_selected=0;}else if(pressed&SCE_CTRL_CROSS){spotify_login_web();}else if(pressed&SCE_CTRL_TRIANGLE){spotify_login_web();}SceTouchData ctd;memset(&ctd,0,sizeof(ctd));sceTouchPeek(SCE_TOUCH_PORT_FRONT,&ctd,1);static int spotify_connect_touch_down=0;int cdown=ctd.reportNum>0;if(cdown&&!spotify_connect_touch_down){int tx=ctd.report[0].x*960/1919;int ty=ctd.report[0].y*544/1087;if(tx>=320&&tx<=640&&ty>=270&&ty<=360){spotify_login_web();}}spotify_connect_touch_down=cdown;}else if(search_view){if(pressed&SCE_CTRL_UP&&result_selected>0)result_selected--;if(pressed&SCE_CTRL_DOWN&&result_selected+1<result_count)result_selected++;if(pressed&SCE_CTRL_CROSS&&result_count){spotify_play_uri(proxy,results[result_selected].uri);search_view=0;spotify_refresh();}if(pressed&SCE_CTRL_SELECT&&result_count)spotify_queue_uri(proxy,results[result_selected].uri);if(pressed&SCE_CTRL_CIRCLE)search_view=0;if(pressed&SCE_CTRL_TRIANGLE){return_mode=MODE_SPOTIFY;mode=MODE_KEYBOARD;input[0]=0;keysel=0;}}else{if(pressed&SCE_CTRL_LEFT){spotify_control_selected=(spotify_control_selected+2)%3;}if(pressed&SCE_CTRL_RIGHT){spotify_control_selected=(spotify_control_selected+1)%3;}if(pressed&SCE_CTRL_CROSS){if(spotify_control_selected==0)spotify_command(proxy,"previous");else if(spotify_control_selected==1)spotify_command(proxy,"toggle");else spotify_command(proxy,"next");snprintf(spotify_notice,sizeof(spotify_notice),"Next sent");}if(pressed&SCE_CTRL_LTRIGGER){spotify_command(proxy,"previous");snprintf(spotify_notice,sizeof(spotify_notice),"Previous sent");}if(pressed&SCE_CTRL_RTRIGGER){spotify_command(proxy,"next");snprintf(spotify_notice,sizeof(spotify_notice),"Next sent");}if(pressed&SCE_CTRL_UP){spotify_volume(proxy,sp.volume+5);spotify_refresh();}if(pressed&SCE_CTRL_DOWN){spotify_volume(proxy,sp.volume-5);spotify_refresh();}if(pressed&SCE_CTRL_SQUARE)spotify_refresh();if(pressed&SCE_CTRL_TRIANGLE){return_mode=MODE_SPOTIFY;mode=MODE_KEYBOARD;input[0]=0;keysel=0;}if(pressed&SCE_CTRL_SELECT){if(spotify_login_web()==0)mode=MODE_WEB;}SceTouchData td;sceTouchPeek(SCE_TOUCH_PORT_FRONT,&td,1);spotify_touch(&td);if(++counter>=180){counter=0;}}}
  vita2d_start_drawing();vita2d_clear_screen();if(mode==MODE_KEYBOARD){keyboard_draw(input,keysel,keyboard_purpose==2?"Edit Proxy URL":(keyboard_purpose==3?"Edit API Key":(keyboard_purpose==4?"Type into webpage":(return_mode==MODE_SPOTIFY?"Spotify search":"Address / Google search"))));}else if(mode==MODE_SPOTIFY){draw_spotify();if(sp.connected)draw_spotify_touch_controls();}else if(mode==MODE_SETTINGS){draw_settings();}else if(frame){vita2d_draw_texture(frame,0,0);draw_browser_chrome();vita2d_draw_rectangle(cursor_x-6,cursor_y-1,13,3,RGBA8(20,255,120,255));vita2d_draw_rectangle(cursor_x-1,cursor_y-6,3,13,RGBA8(20,255,120,255));draw_mini_player();}else{
 draw_text(245,205,RGBA8(242,245,244,255),0.80f,proxy_enabled?(net_proxy_ok?"Proxy ONLINE - browser session not ready":reconnect_stage_text()):"Proxy is OFF");
 vita2d_draw_rectangle(250,245,460,80,RGBA8(35,235,110,255));
 draw_text(330,295,RGBA8(5,25,12,255),0.78f,reconnect_busy?"CONNECTING...":"RECONNECT / REFRESH");
 vita2d_draw_rectangle(250,345,460,72,RGBA8(38,48,44,255));
 draw_text(375,389,RGBA8(242,245,244,255),0.72f,"OPEN SETTINGS");
 draw_text(250,454,RGBA8(165,180,175,255),0.60f,"X/Triangle: reconnect   SELECT: settings   Touch works");
}vita2d_end_drawing();vita2d_swap_buffers();sceKernelDelayThread(16667);}
 if(cover)vita2d_free_texture(cover);if(frame)vita2d_free_texture(frame);if(font)vita2d_free_pgf(font);net_term();vita2d_fini();sceKernelExitProcess(0);return 0;}

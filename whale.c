/* gaming_whale.c — five aquatic environments, compressed
 * build: cc -O2 -std=c11 -o gaming_whale gaming_whale.c -lm
 * keys: WASD/arrows swim, 1-5 travel, Q quit
 */
#define _POSIX_C_SOURCE 2001819L
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define SCR_W 240
#define SCR_H 70
#define FPS 30
#define DT (1.0/(double)FPS)
#define MF 20
#define MH 14
#define MB 90
#define MW 60
#define MG 90

enum { C_DEF,C_WHL,C_FSH,C_BUB,C_WAT,C_WED,C_HUD,C_SND,C_HAZ,C_HZ2,
       C_RCK,C_LIL,C_COR,C_GLW,C_WAV,C_SRF,C_BAD,C_N };
static const char *COL[C_N]={
 "\x1b[0m","\x1b[97m","\x1b[93m","\x1b[96m","\x1b[34m","\x1b[92m",
 "\x1b[95m","\x1b[33m","\x1b[91m","\x1b[35m","\x1b[90m","\x1b[32m",
 "\x1b[38;5;209m","\x1b[38;5;51m","\x1b[97m","\x1b[38;5;39m","\x1b[31m"};

static struct termios sv; static int ts=0,as=0,run=1;
static void rst(void){
 if(as){const char*s="\x1b[0m\x1b[?25h\x1b[?1049l";if(write(1,s,strlen(s))<0){}as=0;}
 if(ts){tcsetattr(0,TCSANOW,&sv);ts=0;}}
static void onsig(int s){(void)s;rst();_exit(0);}
static void setup(void){
 if(tcgetattr(0,&sv)==0){struct termios t=sv;
  t.c_lflag&=(tcflag_t)~(ICANON|ECHO);t.c_iflag&=(tcflag_t)~(IXON|ICRNL|INLCR);
  t.c_cc[VMIN]=0;t.c_cc[VTIME]=0;
  if(tcsetattr(0,TCSANOW,&t)==0)ts=1;}
 int f=fcntl(0,F_GETFL,0); if(f!=-1)fcntl(0,F_SETFL,f|O_NONBLOCK);
 const char*s="\x1b[?1049h\x1b[?25l\x1b[2J";if(write(1,s,strlen(s))<0){}as=1;}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
 return(double)t.tv_sec+(double)t.tv_nsec/1e9;}
static void slp(double s){if(s<=0)return;struct timespec t;
 t.tv_sec=(time_t)s;t.tv_nsec=(long)((s-(double)t.tv_sec)*1e9);nanosleep(&t,0);}
enum{KU=1000,KD,KL,KR};
static int rk(void){unsigned char c;if(read(0,&c,1)<=0)return -1;if(c!=27)return c;
 unsigned char a,b;if(read(0,&a,1)!=1||read(0,&b,1)!=1)return 27;
 if(a=='[')switch(b){case'A':return KU;case'B':return KD;
  case'C':return KR;case'D':return KL;}return 27;}

static int TW=80,TH=24;
static char gc[SCR_H][SCR_W],wc[SCR_H][SCR_W];
static unsigned char go[SCR_H][SCR_W],wo[SCR_H][SCR_W];
static void px(int x,int y,char c,unsigned char o){
 if(x<0||y<0||x>=TW||y>=TH)return;gc[y][x]=c;go[y][x]=o;}
static void tx(int x,int y,const char*s,unsigned char o){
 for(;*s;s++,x++)if(*s!=' ')px(x,y,*s,o);}
static void fclr(void){for(int y=0;y<TH;y++){memset(gc[y],' ',(size_t)TW);
 memset(go[y],C_DEF,(size_t)TW);}}
static void fflush_(void){static char b[1<<19];size_t n=0;int cu=-1;
 for(int y=0;y<TH;y++){char p[24];int m=snprintf(p,sizeof p,"\x1b[%d;1H",y+1);
  if(m>0){memcpy(b+n,p,(size_t)m);n+=(size_t)m;}
  int L=(y==TH-1)?TW-1:TW;
  for(int x=0;x<L;x++){unsigned char c=go[y][x];
   if((int)c!=cu){const char*e=COL[c];size_t l=strlen(e);memcpy(b+n,e,l);n+=l;cu=(int)c;}
   b[n++]=gc[y][x];}}
 memcpy(b+n,"\x1b[0m",4);n+=4;if(write(1,b,n)<0){}}
static void gsz(void){struct winsize w;
 if(ioctl(1,TIOCGWINSZ,&w)==0&&w.ws_col>0&&w.ws_row>0){TW=w.ws_col;TH=w.ws_row;}
 else{TW=80;TH=24;}
 if(TW>SCR_W)TW=SCR_W;if(TH>SCR_H)TH=SCR_H;if(TW<1)TW=1;if(TH<1)TH=1;}

typedef enum{E_LK,E_RV,E_ST,E_SE,E_OC,E_N}En;
typedef struct{const char*nm,*bl;unsigned char cw,cv,cb,cs,cg;
 float cx,cy,f0,f1,fs,hs;int fm,hm,wa;const char*bot;int pt;}Env;
static const Env EV[E_N]={
 {"LAKE","still fresh water",C_WAT,C_WED,C_SND,C_SRF,C_GLW,0,0,
  1.2f,2.2f,1.1f,3.5f,8,3,0,"=",10},
 {"RIVER","flowing downstream",C_SND,C_WED,C_SND,C_SRF,C_GLW,-.55f,0,
  2.5f,4.0f,.9f,3.f,10,3,1,"~",15},
 {"STREAM","rushing shallow water",C_GLW,C_WED,C_RCK,C_WAV,C_GLW,.95f,0,
  3.5f,5.5f,.65f,2.2f,12,4,2,"/\\",20},
 {"SEA","open coastal water",C_WAT,C_WED,C_SND,C_SRF,C_COR,.25f,0,
  1.8f,3.2f,.85f,3.f,12,4,2,"=",25},
 {"OCEAN","the deep blue",C_WAT,C_GLW,C_RCK,C_SRF,C_GLW,-.4f,0,
  2.2f,4.5f,.8f,2.8f,14,5,3,"___",30}};
static const char*FA[E_N][2]={{"<><","><>"},{"<=<",">=>"},
 {"<><","><>"},{"<<><","><>>"},{"<===<",">===>"}};

static int wx[MW],wh_[MW],wn=0;static float gx[MG],gy[MG],gp[MG];static int gn=0;
static En g_e=E_LK;
static void bldw(void){const Env*E=&EV[g_e];
 for(int y=2;y<TH-1;y++)for(int x=0;x<TW;x++){int r=rand()%100;
  if(r<4){wc[y][x]='.';wo[y][x]=E->cw;}
  else if(r<6){wc[y][x]='`';wo[y][x]=E->cw;}
  else if(r<7){wc[y][x]='\'';wo[y][x]=E->cw;}
  else{wc[y][x]=' ';wo[y][x]=C_DEF;}}}
static void bldv(void){wn=0;int c=TW/7;if(c>MW)c=MW;
 for(int i=0;i<c;i++){wx[wn]=rand()%(TW>0?TW:1);int h;
  switch(g_e){case E_LK:h=3+rand()%6;break;case E_RV:h=1+rand()%3;break;
   case E_ST:h=1+rand()%2;break;case E_SE:h=4+rand()%7;break;
   case E_OC:h=5+rand()%8;break;default:h=2+rand()%4;}wh_[wn]=h;wn++;}}
static void bldg(void){gn=0;if(g_e!=E_OC&&g_e!=E_SE)return;
 int c=(g_e==E_OC)?60:25;if(c>MG)c=MG;
 for(int i=0;i<c;i++){gx[i]=rand()%(TW>0?TW:1);
  gy[i]=2+rand()%(TH>4?TH-4:1);gp[i]=(rand()%628)/100.f;}gn=c;}
static void blds(void){bldw();bldv();bldg();}

static float bx[MB],by[MB],bv[MB];static char bc[MB];
static void binit(void){for(int i=0;i<MB;i++){bx[i]=rand()%(TW>0?TW:1);
 by[i]=2+rand()%(TH>3?TH-3:1);bv[i]=1.5f+(rand()%250)/100.f;
 bc[i]=(rand()&1)?'o':'.';}}
static void bupd(double d){for(int i=0;i<MB;i++){by[i]-=bv[i]*(float)d;
 if(by[i]<2){by[i]=(float)(TH-1);bx[i]=rand()%(TW>0?TW:1);}}}

typedef struct{float x,y,vx;int a;}Fish;static Fish fi[MF];
static void fsp(const Env*E){for(int i=0;i<MF;i++){if(fi[i].a)continue;
 int l=rand()&1;float s=E->f0+(rand()%100)/100.f*(E->f1-E->f0);
 fi[i].a=1;int sp=TH-9;if(sp<1)sp=1;fi[i].y=(float)(3+rand()%sp);
 if(l){fi[i].x=-5;fi[i].vx=s;}else{fi[i].x=(float)TW;fi[i].vx=-s;}return;}}

typedef enum{H_N,H_LIL,H_LOG,H_RCK,H_JEL,H_SHK}HK;
typedef struct{float x,y,vx,vy;int w,h,a;float ph;HK k;}Hz;
static Hz hz[MH];
static HK hkof(En e){switch(e){case E_LK:return H_LIL;case E_RV:return H_LOG;
 case E_ST:return H_RCK;case E_SE:return H_JEL;case E_OC:return H_SHK;
 default:return H_N;}}
static void hsp(void){for(int i=0;i<MH;i++){if(hz[i].a)continue;Hz*h=&hz[i];
 h->a=1;h->k=hkof(g_e);h->ph=(rand()%628)/100.f;
 switch(h->k){
  case H_LIL:h->w=4;h->h=1;h->x=rand()%(TW>8?TW-8:1);h->y=2+rand()%2;
   h->vx=h->vy=0;break;
  case H_LOG:h->w=6;h->h=1;h->y=TH-5-rand()%4;
   if(rand()&1){h->x=-7;h->vx=1.1f;}else{h->x=(float)TW;h->vx=-1.1f;}
   h->vy=0;break;
  case H_RCK:h->w=3;h->h=2;h->x=rand()%(TW>6?TW-6:1)+2;
   h->y=TH-4-rand()%2;h->vx=h->vy=0;break;
  case H_JEL:h->w=3;h->h=3;h->x=rand()%(TW>6?TW-6:1)+1;
   h->y=4.f+rand()%(TH>10?TH-10:1);h->vx=0;h->vy=(rand()&1)?-.35f:.35f;break;
  case H_SHK:h->w=6;h->h=1;h->y=3.f+rand()%(TH>8?TH-8:1);
   if(rand()&1){h->x=-7;h->vx=3.5f+(rand()%150)/100.f;}
   else{h->x=(float)TW;h->vx=-3.5f-(rand()%150)/100.f;}h->vy=0;break;
  default:h->a=0;break;}return;}}
static void hupd(double d){for(int i=0;i<MH;i++){Hz*h=&hz[i];if(!h->a)continue;
 h->ph+=(float)d*2.5f;h->x+=h->vx*(float)d*4.f;h->y+=h->vy*(float)d;
 if(h->k==H_JEL){if(h->y<2){h->y=2;h->vy=-h->vy;}
  if(h->y>(float)(TH-4)){h->y=(float)(TH-4);h->vy=-h->vy;}}
 if((h->k==H_LOG||h->k==H_SHK)&&(h->x<-9||h->x>(float)TW+2))h->a=0;}}
static void dhz(const Hz*h){int x=(int)h->x,y=(int)h->y;
 switch(h->k){case H_LIL:tx(x,y,"(@@)",C_LIL);break;
  case H_LOG:tx(x,y,"======",C_SND);break;
  case H_RCK:tx(x,y,".@.",C_RCK);tx(x,y+1,"@@@",C_RCK);break;
  case H_JEL:tx(x,y," _ ",C_HZ2);tx(x,y+1,"(o)",C_HZ2);tx(x,y+2," ' ",C_HZ2);break;
  case H_SHK:if(h->vx>0)tx(x,y,">>=>",C_HAZ);else tx(x,y,"<=<<",C_HAZ);break;}}

static const char*WA[5]={"      .","     \":\"",
 "   ___:____     |\"\\/\"|"," ,'        `.    \\  /",
 " |  O        \\___/  |"};
#define WR 5
#define WC 22
static float WX=10,WY=10,WVX=0,WVY=0;
static int sc=0,eat=0,hp=100;static float ifr=0,fl=0,ban=0;
static void dw(int ox,int oy,unsigned char o){for(int r=0;r<WR;r++){
 const char*s=WA[r];for(int c=0;s[c];c++)if(s[c]!=' ')px(ox+c,oy+r,s[c],o);}}

static void inp(void){int k;while((k=rk())!=-1)switch(k){
 case 'q':case 'Q':case 27:run=0;break;
 case 'a':case 'A':case KL:WVX-=1.3f;break;
 case 'd':case 'D':case KR:WVX+=1.3f;break;
 case 'w':case 'W':case KU:WVY-=1.1f;break;
 case 's':case 'S':case KD:WVY+=1.1f;break;
 case '1':case '2':case '3':case '4':case '5':{
  En w=(En)(k-'1');if(w!=g_e){g_e=w;blds();binit();
   memset(fi,0,sizeof fi);memset(hz,0,sizeof hz);ban=2.f;}break;}
 default:break;}}

static void upd(double d,double*ft,double*ht){const Env*E=&EV[g_e];
 WVX*=.86f;WVY*=.86f;
 if(WVX>2.4f)WVX=2.4f;if(WVX<-2.4f)WVX=-2.4f;
 if(WVY>1.5f)WVY=1.5f;if(WVY<-1.5f)WVY=-1.5f;
 WX+=WVX+E->cx;WY+=WVY+E->cy;
 float mx=(float)(TW-WC-1),my=(float)(TH-WR-1);
 if(mx<1)mx=1;if(my<2)my=2;
 if(WX<1){WX=1;WVX=0;}if(WX>mx){WX=mx;WVX=0;}
 if(WY<2){WY=2;WVY=0;}if(WY>my){WY=my;WVY=0;}
 if(ifr>0)ifr-=(float)d;if(fl>0)fl-=(float)d;if(ban>0)ban-=(float)d;
 if(hp<100&&ifr<=0){static float ra=0;ra+=(float)d;if(ra>=1){ra-=1;hp++;if(hp>100)hp=100;}}
 *ft-=d;if(*ft<=0){*ft=E->fs*(.7+(rand()%60)/100.);fsp(E);}
 *ht-=d;if(*ht<=0){*ht=E->hs*(.8+(rand()%60)/100.);hsp();}
 int bx0=(int)WX+1,bx1=(int)WX+20,by0=(int)WY,by1=(int)WY+4;
 for(int i=0;i<MF;i++){if(!fi[i].a)continue;fi[i].x+=fi[i].vx*(float)d;
  if(fi[i].x<-8||fi[i].x>(float)TW+4){fi[i].a=0;continue;}
  const char*a=FA[g_e][fi[i].vx>0?1:0];int L=(int)strlen(a);
  int fx0=(int)fi[i].x,fx1=fx0+L-1,fy=(int)fi[i].y;
  if(fx1>=bx0&&fx0<=bx1&&fy>=by0&&fy<=by1){fi[i].a=0;sc+=E->pt;eat++;
   if(hp<100)hp+=2;if(hp>100)hp=100;}}
 hupd(d);
 if(ifr<=0)for(int i=0;i<MH;i++){Hz*h=&hz[i];if(!h->a)continue;
  int hx0=(int)h->x,hx1=hx0+h->w-1,hy0=(int)h->y,hy1=hy0+h->h-1;
  if(hx1>=bx0&&hx0<=bx1&&hy1>=by0&&hy0<=by1){hp-=15;ifr=1.5f;fl=.35f;
   if(hp<=0){hp=0;run=0;}break;}}
 bupd(d);}

static void rnd(double t){const Env*E=&EV[g_e];fclr();
 for(int y=2;y<TH-1;y++)for(int x=0;x<TW;x++)if(wc[y][x]!=' ')px(x,y,wc[y][x],wo[y][x]);
 for(int x=0;x<TW;x++){int wy=1;if(E->wa>0){float s=sinf((float)x*.18f+(float)t*2.4f);
  wy=1+(int)(s*(float)E->wa*.5f);}if(wy<1)wy=1;px(x,wy,'~',E->cs);}
 int bl=(int)strlen(E->bot);for(int x=0;x<TW;x++)px(x,TH-1,E->bot[x%bl],E->cb);
 int sw=(int)(t*4);
 for(int i=0;i<wn;i++){int x=wx[i];for(int k=0;k<wh_[i];k++){
  int y=TH-2-k;if(y<2)break;int ph=sw+i*2+k;px(x,y,")(|"[ph%3],E->cv);}}
 for(int i=0;i<gn;i++){float f=sinf(gp[i]+(float)t*1.7f);
  if(f>.2f)px((int)gx[i],(int)gy[i],f>.8f?'*':'.',E->cg);}
 for(int i=0;i<MB;i++)px((int)bx[i],(int)by[i],bc[i],C_BUB);
 for(int i=0;i<MH;i++)if(hz[i].a)dhz(&hz[i]);
 for(int i=0;i<MF;i++){if(!fi[i].a)continue;
  tx((int)fi[i].x,(int)fi[i].y,FA[g_e][fi[i].vx>0?1:0],C_FSH);}
 int blk=(ifr>0)&&(((int)(ifr*18)&1)!=0);
 unsigned char wc_=(fl>0)?C_BAD:C_WHL;if(!blk)dw((int)WX,(int)WY,wc_);
 char hb[16];int hf=hp/10;for(int i=0;i<10;i++)hb[i]=(i<hf)?'#':'-';hb[10]=0;
 char hud[256];const char*flo="";
 if(E->cx<-.3f)flo="  current <<<";else if(E->cx>.3f)flo="  current >>>";
 snprintf(hud,sizeof hud," WHALE | %-6s | score %-5d | hp[%s] | fish %-3d |%s  | 1-5 travel  Q quit ",
  E->nm,sc,hb,eat,flo);tx(0,0,hud,C_HUD);
 if(ban>0&&TW>20){char b[160];snprintf(b,sizeof b," ~~  %s  —  %s  ~~ ",E->nm,E->bl);
  int L=(int)strlen(b),x=(TW-L)/2;if(x<0)x=0;int y=TH/2-1;if(y<3)y=3;
  for(int i=0;i<L;i++){px(x+i,y-1,'-',C_HUD);px(x+i,y+1,'-',C_HUD);}
  tx(x,y,b,C_HUD);}
 fflush_();}

int main(void){atexit(rst);signal(SIGINT,onsig);signal(SIGTERM,onsig);
 setup();srand((unsigned)time(0)^(unsigned)getpid());gsz();g_e=E_LK;blds();binit();
 WX=(float)(TW/4);WY=(float)(TH/2);hp=100;
 double t=0,ft=.4,ht=2;int lw=TW,lh=TH;
 while(run){double fs=now();gsz();
  if(TW!=lw||TH!=lh){lw=TW;lh=TH;blds();binit();if(write(1,"\x1b[2J",4)<0){}}
  inp();if(!run)break;upd(DT,&ft,&ht);rnd(t);t+=DT;
  slp(DT-(now()-fs));}
 rst();printf("\n  Whale's journey ended.\n  score : %d\n  fish  : %d\n"
  "  last habitat: %s\n  time  : %.1fs\n\n",sc,eat,EV[g_e].nm,t);return 0;}

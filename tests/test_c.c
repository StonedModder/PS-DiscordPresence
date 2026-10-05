#include "presence.h"
#include "metadata.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
int main(void){
  char o[64];
  presence_json_escape("Say \"hi\"\\\n\x01", o, sizeof o);
  assert(!strcmp(o, "Say \\\"hi\\\"\\\\\\u000a\\u0001"));
  presence_json_escape("\"\"\"\"", o, 5);           /* truncation never splits an escape */
  assert(!strcmp(o, "\\\"\\\""));
  presence_json_escape(NULL, o, sizeof o); assert(!o[0]);
  struct presence_config c;
  presence_config_defaults(&c); assert(c.show_content_id==1);
  const char *j="{\"show_content_id\": false, \"discord_application_id\": \"123\", \"poll_interval_seconds\": 10}";
  assert(presence_config_parse(j,strlen(j),&c)==0 && !c.show_content_id && c.poll_interval_seconds==10 && !strcmp(c.discord_application_id,"123"));
  struct fg_state_response r; struct presence_snapshot a={0},b;
  memset(&r,0,sizeof r); r.classification=FG_STATE_CLASS_ACTIVE_RUNNING; strcpy(r.titleid,"PPSA01234");
  assert(presence_reduce(&a,&r,&b)==PRESENCE_SET_ACTIVE);
  assert(presence_reduce(&b,&r,&a)==PRESENCE_NO_CHANGE);
  r.classification=FG_STATE_CLASS_SUSPENDED_OR_HOME; assert(presence_reduce(&a,&r,&b)==PRESENCE_CLEAR);
  /* stabilize: same pid with no decisive log event keeps the last state */
  memset(&r,0,sizeof r); r.pid=42; strcpy(r.titleid,"CUSA01127"); r.classification=FG_STATE_CLASS_ACTIVE_RUNNING;
  presence_reduce(NULL,&r,&a);
  r.classification=FG_STATE_CLASS_LOADED_UNKNOWN; presence_stabilize(&a,&r);
  assert(r.classification==FG_STATE_CLASS_ACTIVE_RUNNING);
  r.pid=43; r.classification=FG_STATE_CLASS_LOADED_UNKNOWN; presence_stabilize(&a,&r);
  assert(r.classification==FG_STATE_CLASS_LOADED_UNKNOWN);   /* different process: no carry-over */
  r.pid=42; r.classification=FG_STATE_CLASS_NOT_LOADED; presence_stabilize(&a,&r);
  assert(r.classification==FG_STATE_CLASS_NOT_LOADED);       /* decisive states always win */
  mkdir("t",0700);mkdir("t/user",0700);mkdir("t/user/appmeta",0700);mkdir("t/user/appmeta/PPSA01234",0700);
  FILE*f=fopen("t/user/appmeta/PPSA01234/param.json","w");fputs("{\"localizedParameters\":{\"en-US\":{\"titleName\":\"Ratchet \\\"&\\\" Clank\"}}}",f);fclose(f);
  struct presence_metadata m; assert(presence_metadata_resolve_at("t","PPSA01234",&m));
  presence_json_escape(m.title_name,o,sizeof o); printf("title=%s escaped=%s\n",m.title_name,o);
  puts("C tests ok"); return 0; }

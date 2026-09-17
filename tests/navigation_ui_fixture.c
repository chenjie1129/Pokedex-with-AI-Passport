#include "bestiary_service.h"
#include "capture_engine.h"
#include "game_loop.h"
#include "passport_progress.h"
#include "pocket_policy.h"
#include "user_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define pdPASS 1
#define COLOR_GREEN 0
#define COLOR_CORAL 1
static const int BSP_BESTIARY_STORE_DEFAULT = 0;
/* ENUMS */
typedef enum { BSP_BTN_UP, BSP_BTN_DOWN, BSP_BTN_OK } bsp_btn_t;
typedef enum { BSP_BTN_PRESS, BSP_BTN_CLICK, BSP_BTN_DOUBLE, BSP_BTN_LONG } bsp_btn_ev_t;
static ui_state_t s_state;
static uint64_t clock_ms = 1000, s_capture_deadline_ms, s_encounter_sequence;
static city_bestiary_t s_bestiary, before;
static city_pocket_policy_t s_pocket;
static city_settings_t s_settings, s_settings_draft;
static city_capture_round_t s_round;
static city_creature_stats_t s_current_stats = {39, 52, 43};
static uint16_t s_current_species_id = CITY_SPECIES_CHARMANDER, s_current_place_id = 2;
static uint16_t s_evolution_source_id, s_release_copy_selection, s_owned_selection, s_capture_bond_gain;
static uint32_t s_release_instance_id;
static uint8_t s_memory_page;
static bool s_memory_from_home;
static bool s_visit_remembered;
static uint32_t s_companion_instance_id, s_visit_buddy_id;
static uint8_t s_visit_context, s_visit_gain;
static uint8_t s_companion_selection, s_personality_draw;
static bool s_companion_recovered;
static uint8_t s_attempts, s_home_selection, s_bestiary_selection, s_passport_page,
    s_settings_selection, s_encounter_selection, s_evolution_selection, s_action_selection, s_release_selection;
static bool s_settings_editing, s_settings_error, s_settings_saving, s_save_in_progress,
    s_throw_hit, s_bestiary_ready = true, fail_store;
static int s_battery_soc = 90;
static void *s_tick, *s_marker, *s_ring;
static char status_text[100], cue_text[30];
static void *s_aim_status = status_text, *s_aim_cue = cue_text;
static const char *s_capture_feedback = "Wait";
static write_operation_t s_pending_write;
static unsigned writes, saves, scans;
static bool exercise_worker, fail_task;
static void (*queued_task)(void *);
static uint64_t s_wild_clear_retry_ms;
static city_wild_reward_guard_t s_wild_guard;
static city_bestiary_t durable;
static bool production_request_bestiary_write(write_operation_t op);
static int xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                       void *arg, unsigned priority, void *handle)
{
    (void)name;(void)stack;(void)arg;(void)priority;(void)handle;
    if (fail_task) return 0;
    assert(!queued_task); queued_task=task; return pdPASS;
}
static void vTaskDelete(void *task) { (void)task; }
static void run_write(void) { assert(queued_task); void (*task)(void *)=queued_task; queued_task=NULL; task(NULL); }
static uint64_t now_ms(void) { return clock_ms; }
static unsigned esp_random(void) { return 1; }
static void set_state(ui_state_t state) { s_state = state; }
static bool bsp_lvgl_lock(unsigned ms) { (void)ms; return true; }
static void bsp_lvgl_unlock(void) {}
static void bsp_display_backlight(unsigned brightness) { (void)brightness; }
static const city_settings_t *visible_settings(void) { return &s_settings; }
static city_language_t visible_language(void) { return CITY_LANGUAGE_ENGLISH; }
static const char *tr(const char *text) { return text; }
static void lv_timer_set_period(void *timer, unsigned ms) { (void)timer; (void)ms; }
static void lv_obj_set_x(void *o, int x) { (void)o; (void)x; }
static void lv_obj_set_size(void *o, int x, int y) { (void)o; (void)x; (void)y; }
static void lv_obj_set_pos(void *o, int x, int y) { (void)o; (void)x; (void)y; }
static unsigned lv_color_hex(unsigned c) { return c; }
static void lv_obj_set_style_border_color(void *o, unsigned c, unsigned sel) { (void)o;(void)c;(void)sel; }
static void lv_label_set_text(void *o, const char *text) { snprintf(o,30,"%s",text); }
static void lv_label_set_text_fmt(void *o, const char *fmt, ...) {
    va_list args; va_start(args,fmt); vsnprintf(o,100,fmt,args); va_end(args);
}
static void handle_settings_button(bsp_btn_t button, bool cancel) { (void)button;(void)cancel; }
static void begin_place_scan(void) { ++scans; s_state = UI_SCANNING; }
static void begin_wild_encounter(void) { s_state = UI_ENCOUNTER; }
static bool place_scan_coordinator_passport(city_passport_stamps_t *stamps) { memset(stamps,0,sizeof(*stamps));return true; }
static bool request_bestiary_write(write_operation_t op) { ++writes; if(exercise_worker)return production_request_bestiary_write(op); s_pending_write=op;return true; }
static uint8_t species_selection_index(uint16_t id) { return city_species_index(id); }
static bool bsp_bestiary_store_persist(const city_bestiary_t *b, void *ctx) { (void)ctx;++saves;if(!fail_store)durable=*b;return !fail_store; }
/* PRODUCTION */
static void click(bsp_btn_t key) { on_button(key,BSP_BTN_CLICK,NULL); }
static void hold(void) { on_button(BSP_BTN_OK,BSP_BTN_LONG,NULL); }
static void no_mutation(void) { assert(writes == 0); assert(memcmp(&before,&s_bestiary,sizeof(before)) == 0); }
static void setup_storage_case(write_operation_t op)
{
    fail_store=false;fail_task=false;queued_task=NULL;s_save_in_progress=false;
    memset(&s_pocket,0,sizeof(s_pocket));s_settings_saving=false;
    assert(city_bestiary_import_legacy_count(&s_bestiary,2));
    s_bestiary.buddy_species_id=CITY_SPECIES_CHARMANDER;s_bestiary.buddy_instance_id=1;
    s_bestiary.records[1].friendship=100;s_bestiary.records[1].buddy_places=7;
    s_bestiary.owned[0].current_hp--;
    assert(city_bestiary_is_valid(&s_bestiary));
    s_current_species_id=CITY_SPECIES_BULBASAUR;s_current_place_id=op==WRITE_WILD?0:2;
    s_current_stats=(city_creature_stats_t){45,49,49};s_encounter_sequence=3;
    s_visit_buddy_id=1;s_companion_instance_id=op==WRITE_BUDDY?2:1;
    s_release_instance_id=2;s_evolution_source_id=CITY_SPECIES_CHARMANDER;
    city_wild_reward_guard_init(&s_wild_guard,NULL,clock_ms);
    before=s_bestiary;durable=s_bestiary;s_state=UI_STORAGE_ERROR;s_pending_write=op;
    exercise_worker=true;fail_store=true;writes=0;
}
static void storage_error_replay(void)
{
    const write_operation_t ops[]={WRITE_DISCOVERY,WRITE_CAPTURE,WRITE_WILD,WRITE_BUDDY,
                                  WRITE_EVOLUTION,WRITE_RECOVER,WRITE_RELEASE};
    const ui_state_t success[]={UI_BUDDY_REACTION,UI_CAPTURED,UI_ENCOUNTER,UI_HOME,
                                UI_EVOLVED,UI_COMPANION,UI_RELEASED};
    for(unsigned i=0;i<sizeof(ops)/sizeof(ops[0]);++i) {
        setup_storage_case(ops[i]);
        click(BSP_BTN_OK); assert(s_save_in_progress && queued_task);
        unsigned dispatched=writes;click(BSP_BTN_OK);assert(writes==dispatched);
        run_write();assert(s_state==UI_STORAGE_ERROR && s_pending_write==ops[i] && !s_save_in_progress);
        assert(!memcmp(&before,&s_bestiary,sizeof(before)) && !memcmp(&before,&durable,sizeof(before)));
        fail_store=false;click(BSP_BTN_OK);run_write();
        assert(s_state==success[i] && s_pending_write==WRITE_NONE && !s_save_in_progress);
        assert(!memcmp(&durable,&s_bestiary,sizeof(durable)));
        setup_storage_case(ops[i]);click(BSP_BTN_OK);run_write();
        unsigned saved=saves;click(BSP_BTN_UP);assert(s_state==UI_HOME && s_pending_write==WRITE_NONE);
        assert(!queued_task && saves==saved && !memcmp(&before,&s_bestiary,sizeof(before)));
        setup_storage_case(ops[i]);fail_task=true;click(BSP_BTN_OK);
        assert(s_state==UI_STORAGE_ERROR && !s_save_in_progress && !queued_task);
        fail_task=false;fail_store=false;click(BSP_BTN_OK);run_write();assert(s_state==success[i]);
    }
    puts("Storage UI: 7 production operations preserve state on failure, block double taps, retry, cancel and recover task creation");
}
int main(void) {
    city_bestiary_init(&s_bestiary);
    s_state=UI_ENCOUNTER; click(BSP_BTN_OK); assert(s_state==UI_CAPTURE_READY && s_capture_deadline_ms==0);
    clock_ms+=60000; assert(s_attempts==0); /* Reading has not started a timer. */
    click(BSP_BTN_UP); assert(s_state==UI_ENCOUNTER);
    click(BSP_BTN_OK); click(BSP_BTN_OK); assert(s_state==UI_AIM && s_attempts==3);
    assert(s_capture_deadline_ms==clock_ms+15000);
    click(BSP_BTN_OK); assert(s_state==UI_THROWING && !s_throw_hit && strcmp(s_capture_feedback,"Too early")==0);
    clock_ms+=850; finish_throw(clock_ms); assert(s_state==UI_AIM && s_attempts==2);
    clock_ms=s_round.started_ms+s_round.target_center_ms; update_aim(clock_ms);
    assert(strcmp(cue_text,"NOW")==0 && strstr(status_text,"2 tries"));
    click(BSP_BTN_OK); assert(s_throw_hit); finish_throw(clock_ms);assert(s_state==UI_CATCHING);
    /* Save failure neither grants bond nor changes collection; retry grants exactly once. */
    assert(city_bestiary_capture(&s_bestiary,1,CITY_SPECIES_PIKACHU,1,bsp_bestiary_store_persist,NULL)==CITY_BESTIARY_APPLIED);
    assert(city_bestiary_choose_buddy(&s_bestiary,CITY_SPECIES_PIKACHU,bsp_bestiary_store_persist,NULL)==CITY_BESTIARY_APPLIED);
    s_encounter_sequence=2; before=s_bestiary;fail_store=true;
    unsigned before_failure=saves;
    assert(!persist_capture() && s_capture_bond_gain==0 && saves==before_failure+1);assert(memcmp(&before,&s_bestiary,sizeof(before))==0);
    fail_store=false; assert(persist_capture() && s_capture_bond_gain==3);
    before=s_bestiary;unsigned committed=saves;assert(persist_capture());assert(saves==committed);
    assert(s_capture_bond_gain==3 && memcmp(&before,&s_bestiary,sizeof(before))==0);
    /* Capped bond displays the actual delta, not a promised +3. */
    city_bestiary_record(&s_bestiary,CITY_SPECIES_PIKACHU)->friendship=99;
    s_encounter_sequence=3;s_current_place_id=3;assert(persist_capture() && s_capture_bond_gain==1);
    s_bestiary_selection=city_species_index(CITY_SPECIES_CHARMANDER);before=s_bestiary;writes=0;
    /* Browse a different owned species, then Home, without setting it as buddy. */
    s_state=UI_BESTIARY_DETAIL;click(BSP_BTN_DOWN);assert(s_state==UI_BESTIARY_LIST);
    click(BSP_BTN_DOWN);click(BSP_BTN_UP);click(BSP_BTN_OK);assert(s_state==UI_BESTIARY_DETAIL);
    click(BSP_BTN_OK);assert(s_state==UI_POKEMON_ACTIONS);
    s_action_selection=4;click(BSP_BTN_OK);assert(s_state==UI_BESTIARY_DETAIL);
    click(BSP_BTN_DOWN);hold();assert(s_state==UI_HOME);no_mutation();
    /* Browse both copies, then choose an explicit individual; navigation never writes. */
    s_bestiary_selection=city_species_index(CITY_SPECIES_CHARMANDER);
    s_state=UI_BESTIARY_DETAIL;click(BSP_BTN_UP);assert(s_state==UI_OWNED_DETAIL);
    click(BSP_BTN_DOWN);assert(s_owned_selection==1U);
    click(BSP_BTN_DOWN);assert(s_owned_selection==0U);
    click(BSP_BTN_UP);assert(s_owned_selection==1U);
    click(BSP_BTN_OK);assert(s_state==UI_COMPANION && s_companion_instance_id==3U);no_mutation();
    click(BSP_BTN_DOWN);click(BSP_BTN_OK);no_mutation(); /* full HP Rest cannot write */
    click(BSP_BTN_DOWN);click(BSP_BTN_OK);assert(s_state==UI_OWNED_DETAIL);no_mutation();
    click(BSP_BTN_OK);click(BSP_BTN_OK);assert(writes==1 && s_pending_write==WRITE_BUDDY);
    writes=0;hold();assert(s_state==UI_POKEMON_ACTIONS);no_mutation();
    assert(city_bestiary_damage_instance(&s_bestiary,3,3,bsp_bestiary_store_persist,NULL)==CITY_BESTIARY_APPLIED);
    before=s_bestiary;s_state=UI_OWNED_DETAIL;s_owned_selection=1;
    click(BSP_BTN_OK);click(BSP_BTN_DOWN);click(BSP_BTN_OK);
    assert(writes==1 && s_pending_write==WRITE_RECOVER && s_companion_instance_id==3);
    writes=0;before=s_bestiary;
    /* Memories are reachable from Home and individual actions, with explicit return. */
    s_state=UI_HOME;s_home_selection=4;click(BSP_BTN_OK);
    assert(s_state==UI_MEMORIES && s_memory_from_home && s_companion_instance_id==s_bestiary.buddy_instance_id);
    click(BSP_BTN_DOWN);click(BSP_BTN_OK);assert(s_state==UI_HOME);no_mutation();
    s_state=UI_COMPANION;s_companion_instance_id=3;s_companion_selection=3;
    click(BSP_BTN_OK);assert(s_state==UI_MEMORIES && !s_memory_from_home);
    click(BSP_BTN_OK);assert(s_state==UI_COMPANION && s_companion_selection==3);no_mutation();
    click(BSP_BTN_DOWN);click(BSP_BTN_OK);assert(s_state==UI_POKEMON_ACTIONS);no_mutation();
    /* Release cancellation works for both single and multiple copies. */
    const uint16_t species[] = {CITY_SPECIES_PIKACHU,CITY_SPECIES_CHARMANDER};
    for(unsigned i=0;i<2;++i) {
        s_bestiary_selection=city_species_index(species[i]);
        s_state=UI_RELEASE_PICKER;s_release_copy_selection=0;
        click(BSP_BTN_UP);assert(s_release_copy_selection==city_bestiary_owned_count(&s_bestiary,species[i]));
        click(BSP_BTN_OK);assert(s_state==UI_POKEMON_ACTIONS);no_mutation();
        s_state=UI_RELEASE_PICKER;s_release_copy_selection=0;click(BSP_BTN_OK);
        assert(s_state==UI_RELEASE_CONFIRM && s_release_selection==1);
        click(BSP_BTN_OK);assert(s_state==UI_POKEMON_ACTIONS);no_mutation();
        s_state=UI_RELEASE_PICKER;hold();assert(s_state==UI_POKEMON_ACTIONS);
        s_state=UI_RELEASE_CONFIRM;hold();assert(s_state==UI_POKEMON_ACTIONS);no_mutation();
    }
    /* Cancel is ignored while a write is active; an explicit release still requires confirmation. */
    s_state=UI_RELEASE_CONFIRM;s_save_in_progress=true;hold();assert(s_state==UI_RELEASE_CONFIRM);
    click(BSP_BTN_OK);no_mutation();s_save_in_progress=false;
    s_release_selection=1;click(BSP_BTN_UP);click(BSP_BTN_OK);assert(writes==1 && s_pending_write==WRITE_RELEASE);
    writes=0;
    const ui_state_t errors[]={UI_PLACE_ERROR,UI_PLACE_STORAGE_ERROR,UI_STORAGE_ERROR};
    for(unsigned i=0;i<3;++i){s_state=errors[i];s_pending_write=WRITE_CAPTURE;click(BSP_BTN_UP);assert(s_state==UI_HOME && s_pending_write==WRITE_NONE);no_mutation();}
    s_state=UI_PLACE_ERROR;click(BSP_BTN_OK);assert(scans==1 && s_state==UI_SCANNING);
    s_state=UI_STORAGE_ERROR;s_pending_write=WRITE_CAPTURE;click(BSP_BTN_OK);assert(writes==1 && s_pending_write==WRITE_CAPTURE);writes=0;
    s_state=UI_BESTIARY_LIST;s_bestiary_selection=city_species_index(CITY_SPECIES_SQUIRTLE);
    click(BSP_BTN_OK);assert(s_state==UI_BESTIARY_HINT);click(BSP_BTN_OK);assert(s_state==UI_BESTIARY_LIST);no_mutation();
    s_state=UI_ENCOUNTER;click(BSP_BTN_OK);assert(s_state==UI_AIM); /* Repeat players skip tutorial. */
    clock_ms=s_round.started_ms+s_round.target_center_ms+s_round.target_half_width_ms+1;
    click(BSP_BTN_OK);assert(!s_throw_hit && strcmp(s_capture_feedback,"Too late")==0);
    finish_throw(clock_ms);clock_ms=s_round.started_ms+s_round.duration_ms;update_aim(clock_ms);
    assert(s_attempts==1 && strcmp(s_capture_feedback,"No throw")==0);
    clock_ms=s_capture_deadline_ms;update_aim(clock_ms);assert(s_state==UI_ESCAPED && s_attempts==0);
    s_state=UI_AIM;hold();assert(s_state==UI_ABANDONED && !s_round.active);no_mutation();
    /* Production discovery settlement awards friendship even if capture is skipped. */
    assert(city_bestiary_choose_buddy_instance(&s_bestiary,1,bsp_bestiary_store_persist,NULL)==CITY_BESTIARY_APPLIED);
    s_visit_buddy_id=1;s_current_place_id=4;s_current_species_id=CITY_SPECIES_CHARMANDER;
    assert(city_bestiary_next_encounter_sequence(&s_bestiary,&s_encounter_sequence));
    before=s_bestiary;fail_store=true;assert(!persist_discovery());
    assert(!memcmp(&before,&s_bestiary,sizeof(before)));fail_store=false;
    assert(persist_discovery() && s_visit_gain==5 && s_visit_context==1);
    before=s_bestiary;committed=saves;assert(persist_discovery());
    assert(saves==committed && s_visit_gain==0 && !memcmp(&before,&s_bestiary,sizeof(before)));
    s_state=UI_BUDDY_REACTION;click(BSP_BTN_OK);assert(s_state==UI_ENCOUNTER);
    s_encounter_selection=1;click(BSP_BTN_OK);assert(s_state==UI_ABANDONED);
    assert(city_bestiary_owned_by_id(&s_bestiary,1)->friendship==5);
    puts("Production navigation/capture replay passed; cancellations preserve assets and buddy");
    storage_error_replay();
    return 0;
}

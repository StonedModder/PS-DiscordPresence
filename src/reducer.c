#include "presence.h"
#include <stdio.h>
#include <string.h>

const char *presence_class_name(uint32_t c) {
    switch (c) {
    case FG_STATE_CLASS_NOT_LOADED: return "not_loaded";
    case FG_STATE_CLASS_LOADED_UNKNOWN: return "loaded_unknown";
    case FG_STATE_CLASS_ACTIVE_RUNNING: return "active_running";
    case FG_STATE_CLASS_SUSPENDED_OR_HOME: return "suspended_or_home";
    default: return "unknown";
    }
}
const char *presence_action_name(enum presence_action a) {
    return a == PRESENCE_SET_ACTIVE ? "set_active" : a == PRESENCE_CLEAR ? "clear" : "no_change";
}
static void copy_snapshot(struct presence_snapshot *to, const struct fg_state_response *from) {
    memset(to, 0, sizeof(*to)); to->classification=from->classification; to->confidence=from->confidence; to->pid=from->pid;
    snprintf(to->titleid,sizeof(to->titleid),"%s",from->titleid);
    snprintf(to->contentid,sizeof(to->contentid),"%s",from->contentid);
    snprintf(to->reason,sizeof(to->reason),"%s",from->reason);
}
/* The focus events classify() relies on live in kern.msgbuf, a ring buffer that
   other log output overwrites within minutes. When the same process is still
   loaded and the log no longer holds a decisive event, keep the last decision. */
void presence_stabilize(const struct presence_snapshot *previous, struct fg_state_response *current) {
    if (!previous || !current || current->classification != FG_STATE_CLASS_LOADED_UNKNOWN) return;
    if (current->pid == 0 || current->pid != previous->pid) return;
    if (previous->classification != FG_STATE_CLASS_ACTIVE_RUNNING &&
        previous->classification != FG_STATE_CLASS_SUSPENDED_OR_HOME) return;
    current->classification = previous->classification;
    current->confidence = 0;
    snprintf(current->reason, sizeof(current->reason), "no recent focus event; keeping last state for pid %u", current->pid);
}
enum presence_action presence_reduce(const struct presence_snapshot *previous, const struct fg_state_response *current, struct presence_snapshot *next) {
    int was_active = previous && previous->classification == FG_STATE_CLASS_ACTIVE_RUNNING;
    int is_active = current && current->classification == FG_STATE_CLASS_ACTIVE_RUNNING;
    if (!current || !next) return PRESENCE_NO_CHANGE;
    copy_snapshot(next,current);
    if (is_active && (!was_active || strcmp(previous->titleid,current->titleid))) return PRESENCE_SET_ACTIVE;
    if (!is_active && was_active) return PRESENCE_CLEAR;
    return PRESENCE_NO_CHANGE;
}

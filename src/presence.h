#ifndef PS_DISCORD_PRESENCE_H
#define PS_DISCORD_PRESENCE_H

#include <stddef.h>
#include <stdint.h>
#include "fg_state.h"

#define PS_DISCORD_PRESENCE_PORT 9878u
#define PS_DISCORD_PRESENCE_DATA_DIR "/data/PS-DiscordPresence"
#define PS_DISCORD_PRESENCE_CONFIG_PATH PS_DISCORD_PRESENCE_DATA_DIR "/config.json"

enum presence_action { PRESENCE_NO_CHANGE, PRESENCE_SET_ACTIVE, PRESENCE_CLEAR };

struct presence_config {
    int enabled;
    unsigned poll_interval_seconds;
    int show_title_id;
    int show_content_id;
    char discord_application_id[24];
};

struct presence_snapshot {
    uint32_t classification;
    uint32_t confidence;
    uint32_t pid;
    char titleid[16];
    char contentid[64];
    char reason[160];
};

void presence_config_defaults(struct presence_config *out);
int presence_config_parse(const char *json, size_t len, struct presence_config *out);
int presence_config_serialize(const struct presence_config *cfg, char *out, size_t out_size);
void presence_stabilize(const struct presence_snapshot *previous, struct fg_state_response *current);
enum presence_action presence_reduce(const struct presence_snapshot *previous,
                                     const struct fg_state_response *current,
                                     struct presence_snapshot *next);
const char *presence_action_name(enum presence_action action);
const char *presence_class_name(uint32_t classification);
size_t presence_json_escape(const char *in, char *out, size_t out_size);

#endif

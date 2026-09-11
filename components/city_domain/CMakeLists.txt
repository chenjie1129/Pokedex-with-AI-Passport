idf_component_register(
    SRCS
        "src/bestiary_service.c"
        "src/capture_engine.c"
        "src/encounter_selector.c"
        "src/game_loop.c"
        "src/location_mode.c"
        "src/place_fingerprint.c"
        "src/place_profile_codec.c"
        "src/privacy_tokenizer.c"
        "src/wild_reward_guard.c"
    INCLUDE_DIRS "include"
)

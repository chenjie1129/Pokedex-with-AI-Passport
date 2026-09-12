#include "ui_strings.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    assert(strcmp(ui_text(CITY_LANGUAGE_ENGLISH, "Pokédex"), "Pokédex") == 0);
    assert(strcmp(ui_text(CITY_LANGUAGE_SIMPLIFIED_CHINESE, "Pokédex"), "Pokédex") == 0);
    assert(strcmp(
        ui_text(CITY_LANGUAGE_SIMPLIFIED_CHINESE, "Find a Pokemon!"),
        "寻找宠物小精灵！") == 0);
    assert(strcmp(
        ui_text(CITY_LANGUAGE_SIMPLIFIED_CHINESE, "My Pokemon"),
        "我的宠物小精灵") == 0);
    assert(strcmp(
        ui_species_name(CITY_LANGUAGE_SIMPLIFIED_CHINESE, "Charmander"),
        "小火龙") == 0);
    assert(strcmp(
        ui_species_type(CITY_LANGUAGE_SIMPLIFIED_CHINESE, "Grass / Poison"),
        "草 / 毒") == 0);
    assert(strcmp(
        ui_species_description(
            CITY_LANGUAGE_SIMPLIFIED_CHINESE,
            "It is brave, even against much bigger Pokemon."),
        "即使面对更大的宠物小精灵，它也十分勇敢。") == 0);
    return 0;
}

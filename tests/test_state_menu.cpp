// Save states and rewind in the menu and the settings (issue #40), driven by
// key presses as in test_menu.cpp.
#include "platform/menu.h"

#include <cstdio>
#include <string>

#include "frontend/config.h"
#include "test_framework.h"

using namespace chaotix;

namespace {

bool select_row(Menu& m, const std::string& label) {
    const SDL_Keycode step = m.page_name() == "main" ? SDLK_RIGHT : SDLK_DOWN;
    const std::string first = m.selected_label();
    for (int i = 0; i < 64; ++i) {
        if (m.selected_label() == label) return true;
        m.on_key(step);
        if (m.selected_label() == first) break;
    }
    return m.selected_label() == label;
}

} // namespace

TEST(state_menu, save_states_are_on_the_pause_menu) {
    // SAVE STATE, LOAD STATE and the slot on a page of the pause menu; saving
    // or loading closes the menu and hands the app the slot.
    Config cfg;
    cfg.set_defaults();
    CHECK_EQ(cfg.state_slot, 1);
    Menu m;
    m.bind(cfg);
    Menu::Hooks h;
    int saved = 0, loaded = 0;
    std::string locked;
    h.save_state = [&](int slot) { saved = slot; };
    h.load_state = [&](int slot) { loaded = slot; };
    h.state_info = [](int slot) { return slot == 2 ? std::string("SAVED 2026-10-23 12:00") : std::string("EMPTY"); };
    h.states_locked = [&] { return locked; };
    m.set_hooks(h);
    m.toggle();
    CHECK(select_row(m, "STATES"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "states");
    CHECK(select_row(m, "SLOT"));
    CHECK_STR(m.selected_note(), "EMPTY");
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK_EQ(cfg.state_slot, 2);
    CHECK_STR(m.selected_note(), "SAVED 2026-10-23 12:00");
    CHECK(m.on_key(SDLK_LEFT));
    CHECK(m.on_key(SDLK_LEFT));
    CHECK_EQ(cfg.state_slot, kStateSlots);  // wraps round
    CHECK(select_row(m, "SAVE STATE"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_EQ(saved, kStateSlots);
    CHECK(!m.open());
    CHECK(m.show_page("states"));
    CHECK(select_row(m, "LOAD STATE"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_EQ(loaded, kStateSlots);
    CHECK(!m.open());
    // In a time attack run the page says why they do nothing.
    locked = "NOT IN TIME ATTACK";
    CHECK(m.show_page("states"));
    CHECK(select_row(m, "SLOT"));
    CHECK_STR(m.selected_note(), "NOT IN TIME ATTACK");
    // BACK goes back to the pause menu.
    m.close();
    m.toggle();
    CHECK(select_row(m, "STATES"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(select_row(m, "BACK"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "main");
}

TEST(state_menu, bindings_are_set_and_kept) {
    Config cfg;
    cfg.set_defaults();
    CHECK_STR(cfg.state_save_key, "F8");
    CHECK_STR(cfg.state_load_key, "F9");
    CHECK_STR(cfg.state_slot_key, "F10");
    CHECK_STR(cfg.rewind_key, "Backspace");
    CHECK_STR(cfg.state_save_button, "none");
    CHECK_STR(cfg.state_load_button, "none");
    CHECK_STR(cfg.rewind_button, "lefttrigger");
    CHECK(!cfg.touch_rewind);
    CHECK_EQ(cfg.rewind, rewind_default());
    // None of the keys is one the default bindings use.
    for (const std::string* k : {&cfg.state_save_key, &cfg.state_load_key, &cfg.state_slot_key, &cfg.rewind_key}) {
        for (const auto& kv : cfg.keys) CHECK(kv.second != *k);
        for (const auto& kv : cfg.keys2) CHECK(kv.second != *k);
        CHECK(*k != cfg.ff_key);
    }
    for (const auto& kv : cfg.pads) CHECK(kv.second != cfg.rewind_button);

    Menu m;
    m.bind(cfg);
    m.toggle();
    CHECK(select_row(m, "CONTROLS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(select_row(m, "STATES AND REWIND"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.page_name(), "statekeys");
    // As many rows as a phone's screen holds, no more.
    int rows = 0;
    const std::string first = m.selected_label();
    do { ++rows; m.on_key(SDLK_DOWN); } while (m.selected_label() != first && rows < 64);
    CHECK(rows <= 10);
    CHECK(select_row(m, "REWIND"));
    const bool was = cfg.rewind;
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(cfg.rewind != was);
    CHECK(select_row(m, "TOUCH REWIND"));
    CHECK(m.on_key(SDLK_RIGHT));
    CHECK(cfg.touch_rewind);
    CHECK(select_row(m, "SAVE STATE KEY"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK_STR(m.selected_value(), "PRESS A KEY");
    CHECK(m.on_key(SDLK_F6));
    CHECK_STR(cfg.state_save_key, SDL_GetScancodeName(SDL_SCANCODE_F6));
    CHECK(select_row(m, "REWIND KEY"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(m.on_key(SDLK_ESCAPE));  // gives up, unchanged
    CHECK_STR(cfg.rewind_key, "Backspace");
    CHECK_STR(m.page_name(), "statekeys");
    // The pad buttons never take the menu's, fast-forward's or each other's.
    for (const char* row : {"SAVE STATE PAD", "LOAD STATE PAD", "REWIND PAD"}) {
        CHECK(select_row(m, row));
        for (int i = 0; i < 12; ++i) {
            CHECK(m.on_key(SDLK_RIGHT));
            const std::string* all[] = {&cfg.state_save_button, &cfg.state_load_button, &cfg.rewind_button};
            for (const std::string* b : all) {
                if (*b == "none") continue;
                CHECK(*b != cfg.menu_button);
                CHECK(*b != cfg.ff_button);
                for (const std::string* o : all)
                    if (o != b) CHECK(*o != *b);
            }
        }
    }
    cfg.state_save_button = "rightstick";
    cfg.state_load_button = "none";
    cfg.rewind_button = "lefttrigger";
    cfg.state_slot = 3;
    // And fast-forward's pad steps over them.
    m.close();
    m.toggle();
    CHECK(select_row(m, "CONTROLS"));
    CHECK(m.on_key(SDLK_RETURN));
    CHECK(select_row(m, "FAST FORWARD PAD"));
    for (int i = 0; i < 10; ++i) {
        CHECK(m.on_key(SDLK_RIGHT));
        CHECK(cfg.ff_button != "rightstick");
        CHECK(cfg.ff_button != "lefttrigger");
    }

    const std::string path = "state_keys_test.ini";
    CHECK(cfg.save(path));
    Config back;
    CHECK(back.load(path));
    CHECK_STR(back.state_save_key, cfg.state_save_key);
    CHECK_STR(back.state_load_key, "F9");
    CHECK_STR(back.state_slot_key, "F10");
    CHECK_STR(back.rewind_key, "Backspace");
    CHECK_STR(back.state_save_button, "rightstick");
    CHECK_STR(back.state_load_button, "none");
    CHECK_STR(back.rewind_button, "lefttrigger");
    CHECK(back.touch_rewind);
    CHECK_EQ(back.rewind, cfg.rewind);
    CHECK_EQ(back.state_slot, 3);
    std::remove(path.c_str());
    // A settings file from before save states keeps the defaults; a slot out
    // of range is pulled back into it.
    if (FILE* f = std::fopen(path.c_str(), "w")) {
        std::fputs("[Input]\nFastForwardKey = Tab\n[Extras]\nStateSlot = 9\n", f);
        std::fclose(f);
    }
    CHECK(back.load(path));
    CHECK_STR(back.state_save_key, "F8");
    CHECK_STR(back.rewind_button, "lefttrigger");
    CHECK_EQ(back.rewind, rewind_default());
    CHECK_EQ(back.state_slot, kStateSlots);
    std::remove(path.c_str());
}

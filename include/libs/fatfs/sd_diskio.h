#pragma once

class sd_card;

void bind_sd_card(sd_card& card);
void unbind_sd_card(sd_card& card);

using sd_disk_progress_callback = void (*)(void *context);
void set_sd_disk_progress_callback(sd_disk_progress_callback callback, void *context);

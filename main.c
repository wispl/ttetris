#include "tetris.h"

#include <stdbool.h>

#define MINIAUDIO_IMPLEMENTATION
#include "extern/miniaudio.h"

/* for audio */
static void
data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame)
{
	ma_decoder* decoder = device->pUserData;
	if (decoder == NULL)
		return;
	
	ma_data_source_read_pcm_frames(decoder, output, frame, NULL);
	(void) input;
}

int
main(void)
{
    game_init();
	/* audio */
	ma_result result;
	ma_decoder decoder;
	ma_device_config device_config;
	ma_device device;

	result = ma_decoder_init_file("Tetris.mp3", NULL, &decoder);
	if (result != MA_SUCCESS)
		return -1;

    /* set looping */
    ma_data_source_set_looping(&decoder, MA_TRUE);

    device_config = ma_device_config_init(ma_device_type_playback);
    device_config.noPreSilencedOutputBuffer = true;
    device_config.playback.format   = decoder.outputFormat;
    device_config.playback.channels = decoder.outputChannels;
    device_config.sampleRate        = decoder.outputSampleRate;
    device_config.dataCallback      = data_callback;
    device_config.pUserData         = &decoder;
    device_config.noClip            = true;

    if (ma_device_init(NULL, &device_config, &device) != MA_SUCCESS) {
        ma_decoder_uninit(&decoder);
        return -1;
    }

    if (ma_device_start(&device) != MA_SUCCESS) {
        ma_device_uninit(&device);
        ma_decoder_uninit(&decoder);
        return -1;
    }

    while (game_running()) {
		game_input();
		game_update();
		game_render();
	}

	ma_device_uninit(&device);
	ma_decoder_uninit(&decoder);
	game_destroy();
	return 0;
}

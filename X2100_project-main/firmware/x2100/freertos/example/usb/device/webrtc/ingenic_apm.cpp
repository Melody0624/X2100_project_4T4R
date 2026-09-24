#include <audio_processing.h>
#include <audio_buffer.h>
#include <module_common_types.h>

#include "ingenic_apm.h"

using webrtc::AudioFrame;
using webrtc::AudioBuffer;
using webrtc::AudioProcessing;
using webrtc::EchoCancellation;
using webrtc::GainControl;
using webrtc::HighPassFilter;
using webrtc::LevelEstimator;
using webrtc::NoiseSuppression;
using webrtc::VoiceDetection;
using webrtc::ProcessingConfig;

#define AEC		/* acoustic echo cancellation */
#define AGC		/* automatic gain control */
#define HP		/* High Pass Filter */
#define NS		/* noise suppression */
#define VAD		/* voice activity detection */

AudioProcessing *apm = NULL;
AudioFrame *far_frame = NULL;
AudioFrame *near_frame = NULL;
ProcessingConfig *processing_config;

static int delay_time = 100;	// ms
static float frame_far_v = 1.0;
static float frame_near_v = 1.0;
static int agc_start_volumn = 85;

static int SAMPLE_RATE = 48000;
static int CHANNEL_NUM = 1;
static int SAMPLE_TIME = 10; 	// ms

#define AGC_STARTUP_MIN_VOLUMN 85
#define AGC_MAX_VOLUMN 255

int ingenic_apm_init(int sample_rate)
{
	SAMPLE_RATE = sample_rate;

	int input_sample_rate_hz = SAMPLE_RATE;
	int output_sample_rate_hz = SAMPLE_RATE;
	int reverse_sample_rate_hz = SAMPLE_RATE;
	AudioProcessing::ChannelLayout input_layout = AudioProcessing::kMono;
	AudioProcessing::ChannelLayout output_layout = AudioProcessing::kMono;
	AudioProcessing::ChannelLayout reverse_layout = AudioProcessing::kMono;

	if(agc_start_volumn > AGC_MAX_VOLUMN) {
	  printf("AGC start volume must not exceed %u", AGC_MAX_VOLUMN);
	  return -1;
	}
	webrtc::Config config;
	config.Set<webrtc::ExtendedFilter>(new webrtc::ExtendedFilter(false));
	config.Set<webrtc::DelayAgnostic>(new webrtc::DelayAgnostic(false));
	config.Set<webrtc::ExperimentalAgc>(new webrtc::ExperimentalAgc(false,agc_start_volumn));
	config.Set<webrtc::ExperimentalNs>(new webrtc::ExperimentalNs(false));
	config.Set<webrtc::Intelligibility>(new webrtc::Intelligibility(false));
	
	apm = AudioProcessing::Create(config);
	if(apm == NULL) {
		printf("AudioProcessing::Create() error !\n");
		return -1;
	}
	apm->Initialize(input_sample_rate_hz, output_sample_rate_hz, reverse_sample_rate_hz, input_layout, output_layout, reverse_layout);
/*HP*/
#ifdef HP
	apm->high_pass_filter()->Enable(true);
#endif
#ifdef AEC
	apm->echo_cancellation()->Enable(true);
	apm->echo_cancellation()->enable_drift_compensation(false);
	apm->echo_cancellation()->set_suppression_level(EchoCancellation::kModerateSuppression);
	apm->echo_cancellation()->set_aec_mode(0);
	apm->echo_cancellation()->set_suppression_mode(1);
	apm->echo_cancellation()->set_aec_safe_suppression_value(0.2);//1
	apm->echo_cancellation()->set_far_pow_thd(80000);
	apm->echo_cancellation()->set_mu_parameters(0.000001, 0.95);
	apm->echo_cancellation()->set_cor_thds(0.98, 0.9, 0.9, 0.85);
	apm->echo_cancellation()->enable_delay_logging(false);
#endif

#ifdef NS
/*ns*/
	apm->noise_suppression()->Enable(true);
	//apm->noise_suppression()->set_level(NoiseSuppression::kLow);
	apm->noise_suppression()->set_level(NoiseSuppression::kModerate);
	//apm->noise_suppression()->set_level(NoiseSuppression::kHigh);
	// apm->noise_suppression()->set_level(NoiseSuppression::kVeryHigh);
#endif//NS
	
#ifdef AGC
/*AGC*/
	apm->gain_control()->Enable(true);
	apm->gain_control()->set_mode(GainControl::kFixedDigital);
	apm->gain_control()->set_target_level_dbfs(4);//[0, 31]or negative ???
	apm->gain_control()->set_compression_gain_db(15);//[0,90]
	apm->gain_control()->enable_limiter(false);//When an analog mode is set
	//apm->gain_control()->set_analog_level_limits(10);//[0,255]
	//apm->gain_control()->set_analog_level_limits(300,60000);//[0,65535]
#endif//AGC
#ifdef LE
/*LE*/
	apm->level_estimator()->Enable(false);
#endif//LE
	
#ifdef VAD
/*vad*/
	apm->voice_detection()->Enable(false);
	//apm->voice_detection()->set_likelihood(VoiceDetection::kVeryLowLikelihood);
	//apm->voice_detection()->set_likelihood(VoiceDetection::kLowLikelihood);
	//apm->voice_detection()->set_likelihood(VoiceDetection::kModerateLikelihood);
	apm->voice_detection()->set_likelihood(VoiceDetection::kModerateLikelihood);
	apm->voice_detection()->set_frame_size_ms(10);
#endif//VAD

	delay_time = 100;
	frame_far_v = 1.0;
	frame_near_v = 1.0;

	far_frame = new AudioFrame();
	if(far_frame == NULL) {
		printf("new far AudioFrame error\n");
		return -1;
	}

	near_frame = new AudioFrame();
	if(near_frame == NULL) {
		printf("new near AudioFrame error\n");
		return -1;
	}
	
	return 0;
}


int ingenic_apm_set_far_frame(short *buf)
{
    int i, ret;
    far_frame->num_channels_ = CHANNEL_NUM;
    far_frame->sample_rate_hz_ = SAMPLE_RATE;
    far_frame->samples_per_channel_ = far_frame->sample_rate_hz_ * SAMPLE_TIME / 1000;

    for(i=0;i<(int)far_frame->samples_per_channel_;i++) {
	far_frame->data_[i]=(short)((float)buf[i] * frame_far_v);
    }
    ret = apm->ProcessReverseStream(far_frame);
    if(ret < 0) {
	printf("ProcessReverseStream error : %d---\n", ret);
    }

    return 0;
}

int ingenic_apm_set_near_frame(short *input, short *output)
{
    //printf("ingenic_apm_set_near_frame..\n");
    int i, ret;
    near_frame->num_channels_ = CHANNEL_NUM;
    near_frame->sample_rate_hz_ = SAMPLE_RATE;
    near_frame->samples_per_channel_ = near_frame->sample_rate_hz_ * SAMPLE_TIME / 1000;

    for(i=0;i<(int)near_frame->samples_per_channel_;i++){
	near_frame->data_[i]=(short)((float)input[i] * frame_near_v);
    }
    apm->set_stream_delay_ms(delay_time);

    ret = apm->ProcessStream(near_frame);
    if(ret < 0) {
	printf("ProcessStream() error : %d\n", ret);
    }

    memcpy(output, near_frame->data_, near_frame->samples_per_channel_*sizeof(short));

    return 0;
}

void ingenic_apm_destroy(void)
{
    //dump();
    if(apm != NULL){
	delete apm;//remove this method
	apm = NULL;
    }

    if (far_frame != NULL) {
	delete far_frame;
	far_frame = NULL;
    }

    if (near_frame != NULL) {
	delete near_frame;
	near_frame = NULL;
    }
}


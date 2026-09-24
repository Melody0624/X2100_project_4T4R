#ifndef __USB_UAC_H
#define __USB_UAC_H

/* A.2 Audio Interface Subclass Codes */
#define USB_SUBCLASS_AUDIOCONTROL    0x01
#define USB_SUBCLASS_AUDIOSTREAMING    0x02
#define USB_SUBCLASS_MIDISTREAMING    0x03

/* A.5 Audio Class-Specific AC Interface Descriptor Subtypes */
#define UAC_HEADER            0x01
#define UAC_INPUT_TERMINAL        0x02
#define UAC_OUTPUT_TERMINAL        0x03
#define UAC_MIXER_UNIT            0x04
#define UAC_SELECTOR_UNIT        0x05
#define UAC_FEATURE_UNIT        0x06
#define UAC1_PROCESSING_UNIT        0x07
#define UAC1_EXTENSION_UNIT        0x08

/* A.6 Audio Class-Specific AS Interface Descriptor Subtypes */
#define UAC_AS_GENERAL            0x01
#define UAC_FORMAT_TYPE            0x02
#define UAC_FORMAT_SPECIFIC        0x03

/* A.7 Processing Unit Process Types */
#define UAC_PROCESS_UNDEFINED        0x00
#define UAC_PROCESS_UP_DOWNMIX        0x01
#define UAC_PROCESS_DOLBY_PROLOGIC    0x02
#define UAC_PROCESS_STEREO_EXTENDER    0x03
#define UAC_PROCESS_REVERB        0x04
#define UAC_PROCESS_CHORUS        0x05
#define UAC_PROCESS_DYN_RANGE_COMP    0x06

/* A.8 Audio Class-Specific Endpoint Descriptor Subtypes */
#define UAC_EP_GENERAL            0x01

/* A.9 Audio Class-Specific Request Codes */
#define UAC_SET_            0x00
#define UAC_GET_            0x80

#define UAC__CUR            0x1
#define UAC__MIN            0x2
#define UAC__MAX            0x3
#define UAC__RES            0x4
#define UAC__MEM            0x5

#define UAC_SET_CUR            (UAC_SET_ | UAC__CUR)
#define UAC_GET_CUR            (UAC_GET_ | UAC__CUR)
#define UAC_SET_MIN            (UAC_SET_ | UAC__MIN)
#define UAC_GET_MIN            (UAC_GET_ | UAC__MIN)
#define UAC_SET_MAX            (UAC_SET_ | UAC__MAX)
#define UAC_GET_MAX            (UAC_GET_ | UAC__MAX)
#define UAC_SET_RES            (UAC_SET_ | UAC__RES)
#define UAC_GET_RES            (UAC_GET_ | UAC__RES)
#define UAC_SET_MEM            (UAC_SET_ | UAC__MEM)
#define UAC_GET_MEM            (UAC_GET_ | UAC__MEM)

#define UAC_GET_STAT            0xff

/* A.10 Control Selector Codes */

/* A.10.1 Terminal Control Selectors */
#define UAC_TERM_COPY_PROTECT        0x01

/* A.10.2 Feature Unit Control Selectors */
#define UAC_FU_MUTE            0x01
#define UAC_FU_VOLUME            0x02
#define UAC_FU_BASS            0x03
#define UAC_FU_MID            0x04
#define UAC_FU_TREBLE            0x05
#define UAC_FU_GRAPHIC_EQUALIZER    0x06
#define UAC_FU_AUTOMATIC_GAIN        0x07
#define UAC_FU_DELAY            0x08
#define UAC_FU_BASS_BOOST        0x09
#define UAC_FU_LOUDNESS            0x0a

#define UAC_CONTROL_BIT(CS)    (1 << ((CS) - 1))

/* A.10.3.1 Up/Down-mix Processing Unit Controls Selectors */
#define UAC_UD_ENABLE            0x01
#define UAC_UD_MODE_SELECT        0x02

/* A.10.3.2 Dolby Prologic (tm) Processing Unit Controls Selectors */
#define UAC_DP_ENABLE            0x01
#define UAC_DP_MODE_SELECT        0x02

/* A.10.3.3 3D Stereo Extender Processing Unit Control Selectors */
#define UAC_3D_ENABLE            0x01
#define UAC_3D_SPACE            0x02

/* A.10.3.4 Reverberation Processing Unit Control Selectors */
#define UAC_REVERB_ENABLE        0x01
#define UAC_REVERB_LEVEL        0x02
#define UAC_REVERB_TIME            0x03
#define UAC_REVERB_FEEDBACK        0x04

/* A.10.3.5 Chorus Processing Unit Control Selectors */
#define UAC_CHORUS_ENABLE        0x01
#define UAC_CHORUS_LEVEL        0x02
#define UAC_CHORUS_RATE            0x03
#define UAC_CHORUS_DEPTH        0x04

/* A.10.3.6 Dynamic Range Compressor Unit Control Selectors */
#define UAC_DCR_ENABLE            0x01
#define UAC_DCR_RATE            0x02
#define UAC_DCR_MAXAMPL            0x03
#define UAC_DCR_THRESHOLD        0x04
#define UAC_DCR_ATTACK_TIME        0x05
#define UAC_DCR_RELEASE_TIME        0x06

/* A.10.4 Extension Unit Control Selectors */
#define UAC_XU_ENABLE            0x01

/* MIDI - A.1 MS Class-Specific Interface Descriptor Subtypes */
#define UAC_MS_HEADER            0x01
#define UAC_MIDI_IN_JACK        0x02
#define UAC_MIDI_OUT_JACK        0x03

/* MIDI - A.1 MS Class-Specific Endpoint Descriptor Subtypes */
#define UAC_MS_GENERAL            0x01

/* Terminals - 2.1 USB Terminal Types */
#define UAC_TERMINAL_UNDEFINED        0x100
#define UAC_TERMINAL_STREAMING        0x101
#define UAC_TERMINAL_VENDOR_SPEC    0x1FF

/* channel_masks Audio channel masks */
#define UAC_CH_FRONT_LEFT             0x0001
#define UAC_CH_FRONT_RIGHT            0x0002
#define UAC_CH_FRONT_CENTER           0x0004
#define UAC_CH_LOW_FREQUENCY          0x0008
#define UAC_CH_BACK_LEFT              0x0010
#define UAC_CH_BACK_RIGHT             0x0020
#define UAC_CH_FRONT_LEFT_OF_CENTER   0x0040
#define UAC_CH_FRONT_RIGHT_OF_CENTER  0x0080
#define UAC_CH_BACK_CENTER            0x0100
#define UAC_CH_SIDE_LEFT              0x0200
#define UAC_CH_SIDE_RIGHT             0x0400
#define UAC_CH_TOP_CENTER             0x0800

/* channel_mask_c Audio channel layouts */
#define UAC_CH_LAYOUT_MONO              (UAC_CH_FRONT_CENTER)
#define UAC_CH_LAYOUT_STEREO            (UAC_CH_FRONT_LEFT|UAC_CH_FRONT_RIGHT)
#define UAC_CH_LAYOUT_2POINT1           (UAC_CH_LAYOUT_STEREO|UAC_CH_LOW_FREQUENCY)
#define UAC_CH_LAYOUT_2_1               (UAC_CH_LAYOUT_STEREO|UAC_CH_BACK_CENTER)
#define UAC_CH_LAYOUT_SURROUND          (UAC_CH_LAYOUT_STEREO|UAC_CH_FRONT_CENTER)
#define UAC_CH_LAYOUT_3POINT1           (UAC_CH_LAYOUT_SURROUND|UAC_CH_LOW_FREQUENCY)
#define UAC_CH_LAYOUT_4POINT0           (UAC_CH_LAYOUT_SURROUND|UAC_CH_BACK_CENTER)
#define UAC_CH_LAYOUT_4POINT1           (UAC_CH_LAYOUT_4POINT0|UAC_CH_LOW_FREQUENCY)
#define UAC_CH_LAYOUT_2_2               (UAC_CH_LAYOUT_STEREO|UAC_CH_SIDE_LEFT|UAC_CH_SIDE_RIGHT)
#define UAC_CH_LAYOUT_QUAD              (UAC_CH_LAYOUT_STEREO|UAC_CH_BACK_LEFT|UAC_CH_BACK_RIGHT)
#define UAC_CH_LAYOUT_5POINT0           (UAC_CH_LAYOUT_SURROUND|UAC_CH_SIDE_LEFT|UAC_CH_SIDE_RIGHT)
#define UAC_CH_LAYOUT_5POINT1           (UAC_CH_LAYOUT_5POINT0|UAC_CH_LOW_FREQUENCY)
#define UAC_CH_LAYOUT_5POINT0_BACK      (UAC_CH_LAYOUT_SURROUND|UAC_CH_BACK_LEFT|UAC_CH_BACK_RIGHT)
#define UAC_CH_LAYOUT_5POINT1_BACK      (UAC_CH_LAYOUT_5POINT0_BACK|UAC_CH_LOW_FREQUENCY)
#define UAC_CH_LAYOUT_6POINT0           (UAC_CH_LAYOUT_5POINT0|UAC_CH_BACK_CENTER)
#define UAC_CH_LAYOUT_6POINT0_FRONT     (UAC_CH_LAYOUT_2_2|UAC_CH_FRONT_LEFT_OF_CENTER|UAC_CH_FRONT_RIGHT_OF_CENTER)
#define UAC_CH_LAYOUT_HEXAGONAL         (UAC_CH_LAYOUT_5POINT0_BACK|UAC_CH_BACK_CENTER)
#define UAC_CH_LAYOUT_6POINT1           (UAC_CH_LAYOUT_5POINT1|UAC_CH_BACK_CENTER)
#define UAC_CH_LAYOUT_6POINT1_BACK      (UAC_CH_LAYOUT_5POINT1_BACK|UAC_CH_BACK_CENTER)
#define UAC_CH_LAYOUT_6POINT1_FRONT     (UAC_CH_LAYOUT_6POINT0_FRONT|UAC_CH_LOW_FREQUENCY)
#define UAC_CH_LAYOUT_7POINT0           (UAC_CH_LAYOUT_5POINT0|UAC_CH_BACK_LEFT|UAC_CH_BACK_RIGHT)
#define UAC_CH_LAYOUT_7POINT0_FRONT     (UAC_CH_LAYOUT_5POINT0|UAC_CH_FRONT_LEFT_OF_CENTER|UAC_CH_FRONT_RIGHT_OF_CENTER)
#define UAC_CH_LAYOUT_7POINT1           (UAC_CH_LAYOUT_5POINT1|UAC_CH_BACK_LEFT|UAC_CH_BACK_RIGHT)
#define UAC_CH_LAYOUT_7POINT1_WIDE      (UAC_CH_LAYOUT_5POINT1|UAC_CH_FRONT_LEFT_OF_CENTER|UAC_CH_FRONT_RIGHT_OF_CENTER)
#define UAC_CH_LAYOUT_7POINT1_WIDE_BACK (UAC_CH_LAYOUT_5POINT1_BACK|UAC_CH_FRONT_LEFT_OF_CENTER|UAC_CH_FRONT_RIGHT_OF_CENTER)
#define UAC_CH_LAYOUT_OCTAGONAL         (UAC_CH_LAYOUT_5POINT0|UAC_CH_BACK_LEFT|UAC_CH_BACK_CENTER|UAC_CH_BACK_RIGHT)

#endif /* __USB_UAC_H */

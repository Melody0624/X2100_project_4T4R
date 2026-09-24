#ifndef __USB_AUDIO_H
#define __USB_AUDIO_H

#include <common.h>

/* bInterfaceProtocol values to denote the version of the standard used */
#define UAC_VERSION_1			0x00
#define UAC_VERSION_2			0x20
#define UAC_VERSION_3			0x30

/* Terminal Control Selectors */
/* 4.3.2  Class-Specific AC Interface Descriptor */
struct uac1_ac_header_descriptor {
	u8  bLength;			/* 8 + n */
	u8  bDescriptorType;		/* USB_DT_CS_INTERFACE */
	u8  bDescriptorSubtype;	/* UAC_MS_HEADER */
	u16 bcdADC;			/* 0x0100 */
	u16 wTotalLength;		/* includes Unit and Terminal desc. */
	u8  bInCollection;		/* n */
	u8  baInterfaceNr[];		/* [n] */
} __attribute__ ((packed));

#define UAC_DT_AC_HEADER_SIZE(n)	(8 + (n))

/* As above, but more useful for defining your own descriptors: */
#define DECLARE_UAC_AC_HEADER_DESCRIPTOR(n)			\
struct uac1_ac_header_descriptor_##n {			\
	u8  bLength;						\
	u8  bDescriptorType;					\
	u8  bDescriptorSubtype;				\
	u16 bcdADC;						\
	u16 wTotalLength;					\
	u8  bInCollection;					\
	u8  baInterfaceNr[n];					\
} __attribute__ ((packed))

/* 4.3.2.1 Input Terminal Descriptor */
struct uac_input_terminal_descriptor {
	u8  bLength;			/* in bytes: 12 */
	u8  bDescriptorType;		/* CS_INTERFACE descriptor type */
	u8  bDescriptorSubtype;	/* INPUT_TERMINAL descriptor subtype */
	u8  bTerminalID;		/* Constant uniquely terminal ID */
	u16 wTerminalType;		/* USB Audio Terminal Types */
	u8  bAssocTerminal;		/* ID of the Output Terminal associated */
	u8  bNrChannels;		/* Number of logical output channels */
	u16 wChannelConfig;
	u8  iChannelNames;
	u8  iTerminal;
} __attribute__ ((packed));

#define UAC_DT_INPUT_TERMINAL_SIZE			12

/* Terminals - 2.2 Input Terminal Types */
#define UAC_INPUT_TERMINAL_UNDEFINED			0x200
#define UAC_INPUT_TERMINAL_MICROPHONE			0x201
#define UAC_INPUT_TERMINAL_DESKTOP_MICROPHONE		0x202
#define UAC_INPUT_TERMINAL_PERSONAL_MICROPHONE		0x203
#define UAC_INPUT_TERMINAL_OMNI_DIR_MICROPHONE		0x204
#define UAC_INPUT_TERMINAL_MICROPHONE_ARRAY		0x205
#define UAC_INPUT_TERMINAL_PROC_MICROPHONE_ARRAY	0x206

/* Terminals - control selectors */

#define UAC_TERMINAL_CS_COPY_PROTECT_CONTROL		0x01

/* 4.3.2.2 Output Terminal Descriptor */
struct uac1_output_terminal_descriptor {
	u8  bLength;			/* in bytes: 9 */
	u8  bDescriptorType;		/* CS_INTERFACE descriptor type */
	u8  bDescriptorSubtype;	/* OUTPUT_TERMINAL descriptor subtype */
	u8  bTerminalID;		/* Constant uniquely terminal ID */
	u16 wTerminalType;		/* USB Audio Terminal Types */
	u8  bAssocTerminal;		/* ID of the Input Terminal associated */
	u8  bSourceID;		/* ID of the connected Unit or Terminal*/
	u8  iTerminal;
} __attribute__ ((packed));

#define UAC_DT_OUTPUT_TERMINAL_SIZE			9

/* Terminals - 2.3 Output Terminal Types */
#define UAC_OUTPUT_TERMINAL_UNDEFINED			0x300
#define UAC_OUTPUT_TERMINAL_SPEAKER			0x301
#define UAC_OUTPUT_TERMINAL_HEADPHONES			0x302
#define UAC_OUTPUT_TERMINAL_HEAD_MOUNTED_DISPLAY_AUDIO	0x303
#define UAC_OUTPUT_TERMINAL_DESKTOP_SPEAKER		0x304
#define UAC_OUTPUT_TERMINAL_ROOM_SPEAKER		0x305
#define UAC_OUTPUT_TERMINAL_COMMUNICATION_SPEAKER	0x306
#define UAC_OUTPUT_TERMINAL_LOW_FREQ_EFFECTS_SPEAKER	0x307

/* Terminals - 2.4 Bi-directional Terminal Types */
#define UAC_BIDIR_TERMINAL_UNDEFINED			0x400
#define UAC_BIDIR_TERMINAL_HANDSET			0x401
#define UAC_BIDIR_TERMINAL_HEADSET			0x402
#define UAC_BIDIR_TERMINAL_SPEAKER_PHONE		0x403
#define UAC_BIDIR_TERMINAL_ECHO_SUPPRESSING		0x404
#define UAC_BIDIR_TERMINAL_ECHO_CANCELING		0x405

/* Set bControlSize = 2 as default setting */
#define UAC_DT_FEATURE_UNIT_SIZE(ch)		(7 + ((ch) + 1) * 2)

/* As above, but more useful for defining your own descriptors: */
#define DECLARE_UAC_FEATURE_UNIT_DESCRIPTOR(ch)			\
struct uac_feature_unit_descriptor_##ch {			\
	u8  bLength;						\
	u8  bDescriptorType;					\
	u8  bDescriptorSubtype;				\
	u8  bUnitID;						\
	u8  bSourceID;					\
	u8  bControlSize;					\
	u16 bmaControls[ch + 1];				\
	u8  iFeature;						\
} __attribute__ ((packed))

/* 4.3.2.3 Mixer Unit Descriptor */
struct uac_mixer_unit_descriptor {
	u8 bLength;
	u8 bDescriptorType;
	u8 bDescriptorSubtype;
	u8 bUnitID;
	u8 bNrInPins;
	u8 baSourceID[];
} __attribute__ ((packed));

static __inline__ u8 uac_mixer_unit_bNrChannels(struct uac_mixer_unit_descriptor *desc)
{
	return desc->baSourceID[desc->bNrInPins];
}

static __inline__ u32 uac_mixer_unit_wChannelConfig(struct uac_mixer_unit_descriptor *desc,
						  int protocol)
{
	if (protocol == UAC_VERSION_1)
		return (desc->baSourceID[desc->bNrInPins + 2] << 8) |
			desc->baSourceID[desc->bNrInPins + 1];
	else
		return  (desc->baSourceID[desc->bNrInPins + 4] << 24) |
			(desc->baSourceID[desc->bNrInPins + 3] << 16) |
			(desc->baSourceID[desc->bNrInPins + 2] << 8)  |
			(desc->baSourceID[desc->bNrInPins + 1]);
}

static __inline__ u8 uac_mixer_unit_iChannelNames(struct uac_mixer_unit_descriptor *desc,
						int protocol)
{
	return (protocol == UAC_VERSION_1) ?
		desc->baSourceID[desc->bNrInPins + 3] :
		desc->baSourceID[desc->bNrInPins + 5];
}

static __inline__ u8 *uac_mixer_unit_bmControls(struct uac_mixer_unit_descriptor *desc,
					      int protocol)
{
	switch (protocol) {
	case UAC_VERSION_1:
		return &desc->baSourceID[desc->bNrInPins + 4];
	case UAC_VERSION_2:
		return &desc->baSourceID[desc->bNrInPins + 6];
	case UAC_VERSION_3:
		return &desc->baSourceID[desc->bNrInPins + 2];
	default:
		return NULL;
	}
}

static __inline__ u16 uac3_mixer_unit_wClusterDescrID(struct uac_mixer_unit_descriptor *desc)
{
	return (desc->baSourceID[desc->bNrInPins + 1] << 8) |
		desc->baSourceID[desc->bNrInPins];
}

static __inline__ u8 uac_mixer_unit_iMixer(struct uac_mixer_unit_descriptor *desc)
{
	u8 *raw = (u8 *) desc;
	return raw[desc->bLength - 1];
}

/* 4.3.2.4 Selector Unit Descriptor */
struct uac_selector_unit_descriptor {
	u8 bLength;
	u8 bDescriptorType;
	u8 bDescriptorSubtype;
	u8 bUintID;
	u8 bNrInPins;
	u8 baSourceID[];
} __attribute__ ((packed));

static __inline__ u8 uac_selector_unit_iSelector(struct uac_selector_unit_descriptor *desc)
{
	u8 *raw = (u8 *) desc;
	return raw[desc->bLength - 1];
}

/* 4.3.2.5 Feature Unit Descriptor */
struct uac_feature_unit_descriptor {
	u8 bLength;
	u8 bDescriptorType;
	u8 bDescriptorSubtype;
	u8 bUnitID;
	u8 bSourceID;
	u8 bControlSize;
	u8 bmaControls[0]; /* variable length */
} __attribute__((packed));

static __inline__ u8 uac_feature_unit_iFeature(struct uac_feature_unit_descriptor *desc)
{
	u8 *raw = (u8 *) desc;
	return raw[desc->bLength - 1];
}

/* 4.3.2.6 Processing Unit Descriptors */
struct uac_processing_unit_descriptor {
	u8 bLength;
	u8 bDescriptorType;
	u8 bDescriptorSubtype;
	u8 bUnitID;
	u16 wProcessType;
	u8 bNrInPins;
	u8 baSourceID[];
} __attribute__ ((packed));

static __inline__ u8 uac_processing_unit_bNrChannels(struct uac_processing_unit_descriptor *desc)
{
	return desc->baSourceID[desc->bNrInPins];
}

static __inline__ u32 uac_processing_unit_wChannelConfig(struct uac_processing_unit_descriptor *desc,
						       int protocol)
{
	if (protocol == UAC_VERSION_1)
		return (desc->baSourceID[desc->bNrInPins + 2] << 8) |
			desc->baSourceID[desc->bNrInPins + 1];
	else
		return  (desc->baSourceID[desc->bNrInPins + 4] << 24) |
			(desc->baSourceID[desc->bNrInPins + 3] << 16) |
			(desc->baSourceID[desc->bNrInPins + 2] << 8)  |
			(desc->baSourceID[desc->bNrInPins + 1]);
}

static __inline__ u8 uac_processing_unit_iChannelNames(struct uac_processing_unit_descriptor *desc,
						     int protocol)
{
	return (protocol == UAC_VERSION_1) ?
		desc->baSourceID[desc->bNrInPins + 3] :
		desc->baSourceID[desc->bNrInPins + 5];
}

static __inline__ u8 uac_processing_unit_bControlSize(struct uac_processing_unit_descriptor *desc,
						    int protocol)
{
	switch (protocol) {
	case UAC_VERSION_1:
		return desc->baSourceID[desc->bNrInPins + 4];
	case UAC_VERSION_2:
		return 2; /* in UAC2, this value is constant */
	case UAC_VERSION_3:
		return 4; /* in UAC3, this value is constant */
	default:
		return 1;
	}
}

static __inline__ u8 *uac_processing_unit_bmControls(struct uac_processing_unit_descriptor *desc,
						   int protocol)
{
	switch (protocol) {
	case UAC_VERSION_1:
		return &desc->baSourceID[desc->bNrInPins + 5];
	case UAC_VERSION_2:
		return &desc->baSourceID[desc->bNrInPins + 6];
	case UAC_VERSION_3:
		return &desc->baSourceID[desc->bNrInPins + 2];
	default:
		return NULL;
	}
}

static __inline__ u8 uac_processing_unit_iProcessing(struct uac_processing_unit_descriptor *desc,
						   int protocol)
{
	u8 control_size = uac_processing_unit_bControlSize(desc, protocol);

	switch (protocol) {
	case UAC_VERSION_1:
	case UAC_VERSION_2:
	default:
		return *(uac_processing_unit_bmControls(desc, protocol)
			 + control_size);
	case UAC_VERSION_3:
		return 0; /* UAC3 does not have this field */
	}
}

static __inline__ u8 *uac_processing_unit_specific(struct uac_processing_unit_descriptor *desc,
						 int protocol)
{
	u8 control_size = uac_processing_unit_bControlSize(desc, protocol);

	switch (protocol) {
	case UAC_VERSION_1:
	case UAC_VERSION_2:
	default:
		return uac_processing_unit_bmControls(desc, protocol)
			+ control_size + 1;
	case UAC_VERSION_3:
		return uac_processing_unit_bmControls(desc, protocol)
			+ control_size;
	}
}

/*
 * Extension Unit (XU) has almost compatible layout with Processing Unit, but
 * on UAC2, it has a different bmControls size (bControlSize); it's 1 byte for
 * XU while 2 bytes for PU.  The last iExtension field is a one-byte index as
 * well as iProcessing field of PU.
 */
static __inline__ u8 uac_extension_unit_bControlSize(struct uac_processing_unit_descriptor *desc,
						   int protocol)
{
	switch (protocol) {
	case UAC_VERSION_1:
		return desc->baSourceID[desc->bNrInPins + 4];
	case UAC_VERSION_2:
		return 1; /* in UAC2, this value is constant */
	case UAC_VERSION_3:
		return 4; /* in UAC3, this value is constant */
	default:
		return 1;
	}
}

static __inline__ u8 uac_extension_unit_iExtension(struct uac_processing_unit_descriptor *desc,
						 int protocol)
{
	u8 control_size = uac_extension_unit_bControlSize(desc, protocol);

	switch (protocol) {
	case UAC_VERSION_1:
	case UAC_VERSION_2:
	default:
		return *(uac_processing_unit_bmControls(desc, protocol)
			 + control_size);
	case UAC_VERSION_3:
		return 0; /* UAC3 does not have this field */
	}
}

/* 4.5.2 Class-Specific AS Interface Descriptor */
struct uac1_as_header_descriptor {
	u8  bLength;			/* in bytes: 7 */
	u8  bDescriptorType;		/* USB_DT_CS_INTERFACE */
	u8  bDescriptorSubtype;	/* AS_GENERAL */
	u8  bTerminalLink;		/* Terminal ID of connected Terminal */
	u8  bDelay;			/* Delay introduced by the data path */
	u16 wFormatTag;		/* The Audio Data Format */
} __attribute__ ((packed));

#define UAC_DT_AS_HEADER_SIZE		7

/* Formats - A.1.1 Audio Data Format Type I Codes */
#define UAC_FORMAT_TYPE_I_UNDEFINED	0x0
#define UAC_FORMAT_TYPE_I_PCM		0x1
#define UAC_FORMAT_TYPE_I_PCM8		0x2
#define UAC_FORMAT_TYPE_I_IEEE_FLOAT	0x3
#define UAC_FORMAT_TYPE_I_ALAW		0x4
#define UAC_FORMAT_TYPE_I_MULAW		0x5

struct uac_format_type_i_continuous_descriptor {
	u8  bLength;			/* in bytes: 8 + (ns * 3) */
	u8  bDescriptorType;		/* USB_DT_CS_INTERFACE */
	u8  bDescriptorSubtype;	/* FORMAT_TYPE */
	u8  bFormatType;		/* FORMAT_TYPE_1 */
	u8  bNrChannels;		/* physical channels in the stream */
	u8  bSubframeSize;		/* */
	u8  bBitResolution;
	u8  bSamFreqType;
	u8  tLowerSamFreq[3];
	u8  tUpperSamFreq[3];
} __attribute__ ((packed));

#define UAC_FORMAT_TYPE_I_CONTINUOUS_DESC_SIZE	14

struct uac_format_type_i_discrete_descriptor {
	u8  bLength;			/* in bytes: 8 + (ns * 3) */
	u8  bDescriptorType;		/* USB_DT_CS_INTERFACE */
	u8  bDescriptorSubtype;	/* FORMAT_TYPE */
	u8  bFormatType;		/* FORMAT_TYPE_1 */
	u8  bNrChannels;		/* physical channels in the stream */
	u8  bSubframeSize;		/* */
	u8  bBitResolution;
	u8  bSamFreqType;
	u8  tSamFreq[][3];
} __attribute__ ((packed));

#define DECLARE_UAC_FORMAT_TYPE_I_DISCRETE_DESC(n)		\
struct uac_format_type_i_discrete_descriptor_##n {		\
	u8  bLength;						\
	u8  bDescriptorType;					\
	u8  bDescriptorSubtype;				\
	u8  bFormatType;					\
	u8  bNrChannels;					\
	u8  bSubframeSize;					\
	u8  bBitResolution;					\
	u8  bSamFreqType;					\
	u8  tSamFreq[n][3];					\
} __attribute__ ((packed))

#define UAC_FORMAT_TYPE_I_DISCRETE_DESC_SIZE(n)	(8 + (n * 3))

struct uac_format_type_i_ext_descriptor {
	u8 bLength;
	u8 bDescriptorType;
	u8 bDescriptorSubtype;
	u8 bFormatType;
	u8 bSubslotSize;
	u8 bBitResolution;
	u8 bHeaderLength;
	u8 bControlSize;
	u8 bSideBandProtocol;
} __attribute__((packed));

/* Formats - Audio Data Format Type I Codes */

#define UAC_FORMAT_TYPE_II_MPEG	0x1001
#define UAC_FORMAT_TYPE_II_AC3	0x1002

struct uac_format_type_ii_discrete_descriptor {
	u8 bLength;
	u8 bDescriptorType;
	u8 bDescriptorSubtype;
	u8 bFormatType;
	u16 wMaxBitRate;
	u16 wSamplesPerFrame;
	u8 bSamFreqType;
	u8 tSamFreq[][3];
} __attribute__((packed));

struct uac_format_type_ii_ext_descriptor {
	u8 bLength;
	u8 bDescriptorType;
	u8 bDescriptorSubtype;
	u8 bFormatType;
	u16 wMaxBitRate;
	u16 wSamplesPerFrame;
	u8 bHeaderLength;
	u8 bSideBandProtocol;
} __attribute__((packed));

/* type III */
#define UAC_FORMAT_TYPE_III_IEC1937_AC3	0x2001
#define UAC_FORMAT_TYPE_III_IEC1937_MPEG1_LAYER1	0x2002
#define UAC_FORMAT_TYPE_III_IEC1937_MPEG2_NOEXT	0x2003
#define UAC_FORMAT_TYPE_III_IEC1937_MPEG2_EXT	0x2004
#define UAC_FORMAT_TYPE_III_IEC1937_MPEG2_LAYER1_LS	0x2005
#define UAC_FORMAT_TYPE_III_IEC1937_MPEG2_LAYER23_LS	0x2006

/* Formats - A.2 Format Type Codes */
#define UAC_FORMAT_TYPE_UNDEFINED	0x0
#define UAC_FORMAT_TYPE_I		0x1
#define UAC_FORMAT_TYPE_II		0x2
#define UAC_FORMAT_TYPE_III		0x3
#define UAC_EXT_FORMAT_TYPE_I		0x81
#define UAC_EXT_FORMAT_TYPE_II		0x82
#define UAC_EXT_FORMAT_TYPE_III		0x83

struct uac_iso_endpoint_descriptor {
	u8  bLength;			/* in bytes: 7 */
	u8  bDescriptorType;		/* USB_DT_CS_ENDPOINT */
	u8  bDescriptorSubtype;	/* EP_GENERAL */
	u8  bmAttributes;
	u8  bLockDelayUnits;
	u16 wLockDelay;
} __attribute__((packed));
#define UAC_ISO_ENDPOINT_DESC_SIZE	7

#define UAC_EP_CS_ATTR_SAMPLE_RATE	0x01
#define UAC_EP_CS_ATTR_PITCH_CONTROL	0x02
#define UAC_EP_CS_ATTR_FILL_MAX		0x80

/* status word format (3.7.1.1) */

#define UAC1_STATUS_TYPE_ORIG_MASK		0x0f
#define UAC1_STATUS_TYPE_ORIG_AUDIO_CONTROL_IF	0x0
#define UAC1_STATUS_TYPE_ORIG_AUDIO_STREAM_IF	0x1
#define UAC1_STATUS_TYPE_ORIG_AUDIO_STREAM_EP	0x2

#define UAC1_STATUS_TYPE_IRQ_PENDING		(1 << 7)
#define UAC1_STATUS_TYPE_MEM_CHANGED		(1 << 6)

struct uac1_status_word {
	u8 bStatusType;
	u8 bOriginator;
} __attribute__((packed));

#ifndef __USB_AUDIO_V2_H
#define __USB_AUDIO_V2_H

/* v1.0 and v2.0 of this standard have many things in common. For the rest
 * of the definitions, please refer to audio.h */

/*
 * bmControl field decoders
 *
 * From the USB Audio spec v2.0:
 *
 *   bmaControls() is a (ch+1)-element array of 4-byte bitmaps,
 *   each containing a set of bit pairs. If a Control is present,
 *   it must be Host readable. If a certain Control is not
 *   present then the bit pair must be set to 0b00.
 *   If a Control is present but read-only, the bit pair must be
 *   set to 0b01. If a Control is also Host programmable, the bit
 *   pair must be set to 0b11. The value 0b10 is not allowed.
 *
 */

#include <stdbool.h>

static inline bool uac_v2v3_control_is_readable(u32 bmControls, u8 control)
{
	return (bmControls >> ((control - 1) * 2)) & 0x1;
}

static inline bool uac_v2v3_control_is_writeable(u32 bmControls, u8 control)
{
	return (bmControls >> ((control - 1) * 2)) & 0x2;
}

/* 4.7.2 Class-Specific AC Interface Descriptor */
struct uac2_ac_header_descriptor {
	u8  bLength;			/* 9 */
	u8  bDescriptorType;		/* USB_DT_CS_INTERFACE */
	u8  bDescriptorSubtype;	/* UAC_MS_HEADER */
	u16 bcdADC;			/* 0x0200 */
	u8  bCategory;
	u16 wTotalLength;		/* includes Unit and Terminal desc. */
	u8  bmControls;
} __packed;

/* 2.3.1.6 Type I Format Type Descriptor (Frmts20 final.pdf)*/
struct uac2_format_type_i_descriptor {
	u8  bLength;			/* in bytes: 6 */
	u8  bDescriptorType;		/* USB_DT_CS_INTERFACE */
	u8  bDescriptorSubtype;	/* FORMAT_TYPE */
	u8  bFormatType;		/* FORMAT_TYPE_1 */
	u8  bSubslotSize;		/* {1,2,3,4} */
	u8  bBitResolution;
} __packed;

/* 4.7.2.1 Clock Source Descriptor */

struct uac_clock_source_descriptor {
	u8 bLength;
	u8 bDescriptorType;
	u8 bDescriptorSubtype;
	u8 bClockID;
	u8 bmAttributes;
	u8 bmControls;
	u8 bAssocTerminal;
	u8 iClockSource;
} __attribute__((packed));

/* bmAttribute fields */
#define UAC_CLOCK_SOURCE_TYPE_EXT	0x0
#define UAC_CLOCK_SOURCE_TYPE_INT_FIXED	0x1
#define UAC_CLOCK_SOURCE_TYPE_INT_VAR	0x2
#define UAC_CLOCK_SOURCE_TYPE_INT_PROG	0x3
#define UAC_CLOCK_SOURCE_SYNCED_TO_SOF	(1 << 2)

/* 4.7.2.2 Clock Source Descriptor */

struct uac_clock_selector_descriptor {
	u8 bLength;
	u8 bDescriptorType;
	u8 bDescriptorSubtype;
	u8 bClockID;
	u8 bNrInPins;
	u8 baCSourceID[];
	/* bmControls and iClockSource omitted */
} __attribute__((packed));

/* 4.7.2.3 Clock Multiplier Descriptor */

struct uac_clock_multiplier_descriptor {
	u8 bLength;
	u8 bDescriptorType;
	u8 bDescriptorSubtype;
	u8 bClockID;
	u8 bCSourceID;
	u8 bmControls;
	u8 iClockMultiplier;
} __attribute__((packed));

/* 4.7.2.4 Input terminal descriptor */

struct uac2_input_terminal_descriptor {
	u8 bLength;
	u8 bDescriptorType;
	u8 bDescriptorSubtype;
	u8 bTerminalID;
	u16 wTerminalType;
	u8 bAssocTerminal;
	u8 bCSourceID;
	u8 bNrChannels;
	u32 bmChannelConfig;
	u8 iChannelNames;
	u16 bmControls;
	u8 iTerminal;
} __attribute__((packed));

/* 4.7.2.5 Output terminal descriptor */

struct uac2_output_terminal_descriptor {
	u8 bLength;
	u8 bDescriptorType;
	u8 bDescriptorSubtype;
	u8 bTerminalID;
	u16 wTerminalType;
	u8 bAssocTerminal;
	u8 bSourceID;
	u8 bCSourceID;
	u16 bmControls;
	u8 iTerminal;
} __attribute__((packed));

/* 4.7.2.8 Feature Unit Descriptor */

struct uac2_feature_unit_descriptor {
	u8 bLength;
	u8 bDescriptorType;
	u8 bDescriptorSubtype;
	u8 bUnitID;
	u8 bSourceID;
	/* bmaControls is actually u32,
	 * but u8 is needed for the hybrid parser */
	u8 bmaControls[]; /* variable length */
} __attribute__((packed));

#define UAC2_DT_FEATURE_UNIT_SIZE(ch)		(6 + ((ch) + 1) * 4)

/* As above, but more useful for defining your own descriptors: */
#define DECLARE_UAC2_FEATURE_UNIT_DESCRIPTOR(ch)		\
struct uac2_feature_unit_descriptor_##ch {			\
	u8  bLength;						\
	u8  bDescriptorType;					\
	u8  bDescriptorSubtype;				\
	u8  bUnitID;						\
	u8  bSourceID;					\
	u32 bmaControls[ch + 1];				\
	u8  iFeature;						\
} __packed

/* 4.7.2.10 Effect Unit Descriptor */

struct uac2_effect_unit_descriptor {
	u8 bLength;
	u8 bDescriptorType;
	u8 bDescriptorSubtype;
	u8 bUnitID;
	u16 wEffectType;
	u8 bSourceID;
	u8 bmaControls[]; /* variable length */
} __attribute__((packed));

/* 4.9.2 Class-Specific AS Interface Descriptor */

struct uac2_as_header_descriptor {
	u8 bLength;
	u8 bDescriptorType;
	u8 bDescriptorSubtype;
	u8 bTerminalLink;
	u8 bmControls;
	u8 bFormatType;
	u32 bmFormats;
	u8 bNrChannels;
	u32 bmChannelConfig;
	u8 iChannelNames;
} __attribute__((packed));

#define UAC2_FORMAT_TYPE_I_RAW_DATA	(1 << 31)

/* 4.10.1.2 Class-Specific AS Isochronous Audio Data Endpoint Descriptor */

struct uac2_iso_endpoint_descriptor {
	u8  bLength;			/* in bytes: 8 */
	u8  bDescriptorType;		/* USB_DT_CS_ENDPOINT */
	u8  bDescriptorSubtype;	/* EP_GENERAL */
	u8  bmAttributes;
	u8  bmControls;
	u8  bLockDelayUnits;
	u16 wLockDelay;
} __attribute__((packed));

#define UAC2_CONTROL_PITCH		(3 << 0)
#define UAC2_CONTROL_DATA_OVERRUN	(3 << 2)
#define UAC2_CONTROL_DATA_UNDERRUN	(3 << 4)

/* 5.2.5.4.2 Connector Control Parameter Block */
struct uac2_connectors_ctl_blk {
	u8 bNrChannels;
	u32 bmChannelConfig;
	u8 iChannelNames;
} __attribute__((packed));

/* 6.1 Interrupt Data Message */

#define UAC2_INTERRUPT_DATA_MSG_VENDOR	(1 << 0)
#define UAC2_INTERRUPT_DATA_MSG_EP	(1 << 1)

struct uac2_interrupt_data_msg {
	u8 bInfo;
	u8 bAttribute;
	u16 wValue;
	u16 wIndex;
} __attribute__((packed));

/* A.7 Audio Function Category Codes */
#define UAC2_FUNCTION_SUBCLASS_UNDEFINED	0x00
#define UAC2_FUNCTION_DESKTOP_SPEAKER		0x01
#define UAC2_FUNCTION_HOME_THEATER		0x02
#define UAC2_FUNCTION_MICROPHONE		0x03
#define UAC2_FUNCTION_HEADSET			0x04
#define UAC2_FUNCTION_TELEPHONE			0x05
#define UAC2_FUNCTION_CONVERTER			0x06
#define UAC2_FUNCTION_SOUND_RECORDER		0x07
#define UAC2_FUNCTION_IO_BOX			0x08
#define UAC2_FUNCTION_MUSICAL_INSTRUMENT	0x09
#define UAC2_FUNCTION_PRO_AUDIO			0x0a
#define UAC2_FUNCTION_AUDIO_VIDEO		0x0b
#define UAC2_FUNCTION_CONTROL_PANEL		0x0c
#define UAC2_FUNCTION_OTHER			0xff

/* A.9 Audio Class-Specific AC Interface Descriptor Subtypes */
/* see audio.h for the rest, which is identical to v1 */
#define UAC2_EFFECT_UNIT			0x07
#define UAC2_PROCESSING_UNIT_V2		0x08
#define UAC2_EXTENSION_UNIT_V2		0x09
#define UAC2_CLOCK_SOURCE		0x0a
#define UAC2_CLOCK_SELECTOR		0x0b
#define UAC2_CLOCK_MULTIPLIER		0x0c
#define UAC2_SAMPLE_RATE_CONVERTER	0x0d

/* A.10 Audio Class-Specific AS Interface Descriptor Subtypes */
/* see audio.h for the rest, which is identical to v1 */
#define UAC2_ENCODER			0x03
#define UAC2_DECODER			0x04

/* A.11 Effect Unit Effect Types */
#define UAC2_EFFECT_UNDEFINED		0x00
#define UAC2_EFFECT_PARAM_EQ		0x01
#define UAC2_EFFECT_REVERB		0x02
#define UAC2_EFFECT_MOD_DELAY		0x03
#define UAC2_EFFECT_DYN_RANGE_COMP	0x04

/* A.12 Processing Unit Process Types */
#define UAC2_PROCESS_UNDEFINED		0x00
#define UAC2_PROCESS_UP_DOWNMIX		0x01
#define UAC2_PROCESS_DOLBY_PROLOCIC	0x02
#define UAC2_PROCESS_STEREO_EXTENDER	0x03

/* A.14 Audio Class-Specific Request Codes */
#define UAC2_CS_CUR			0x01
#define UAC2_CS_RANGE			0x02
#define UAC2_CS_MEM			0x03

/* A.15 Encoder Type Codes */
#define UAC2_ENCODER_UNDEFINED		0x00
#define UAC2_ENCODER_OTHER		0x01
#define UAC2_ENCODER_MPEG		0x02
#define UAC2_ENCODER_AC3		0x03
#define UAC2_ENCODER_WMA		0x04
#define UAC2_ENCODER_DTS		0x05

/* A.16 Decoder Type Codes */
#define UAC2_DECODER_UNDEFINED		0x00
#define UAC2_DECODER_OTHER		0x01
#define UAC2_DECODER_MPEG		0x02
#define UAC2_DECODER_AC3		0x03
#define UAC2_DECODER_WMA		0x04
#define UAC2_DECODER_DTS		0x05

/* A.17.1 Clock Source Control Selectors */
#define UAC2_CS_UNDEFINED		0x00
#define UAC2_CS_CONTROL_SAM_FREQ	0x01
#define UAC2_CS_CONTROL_CLOCK_VALID	0x02

/* A.17.2 Clock Selector Control Selectors */
#define UAC2_CX_UNDEFINED		0x00
#define UAC2_CX_CLOCK_SELECTOR		0x01

/* A.17.3 Clock Multiplier Control Selectors */
#define UAC2_CM_UNDEFINED		0x00
#define UAC2_CM_NUMERATOR		0x01
#define UAC2_CM_DENOMINTATOR		0x02

/* A.17.4 Terminal Control Selectors */
#define UAC2_TE_UNDEFINED		0x00
#define UAC2_TE_COPY_PROTECT		0x01
#define UAC2_TE_CONNECTOR		0x02
#define UAC2_TE_OVERLOAD		0x03
#define UAC2_TE_CLUSTER			0x04
#define UAC2_TE_UNDERFLOW		0x05
#define UAC2_TE_OVERFLOW		0x06
#define UAC2_TE_LATENCY			0x07

/* A.17.5 Mixer Control Selectors */
#define UAC2_MU_UNDEFINED		0x00
#define UAC2_MU_MIXER			0x01
#define UAC2_MU_CLUSTER			0x02
#define UAC2_MU_UNDERFLOW		0x03
#define UAC2_MU_OVERFLOW		0x04
#define UAC2_MU_LATENCY			0x05

/* A.17.6 Selector Control Selectors */
#define UAC2_SU_UNDEFINED		0x00
#define UAC2_SU_SELECTOR		0x01
#define UAC2_SU_LATENCY			0x02

/* A.17.7 Feature Unit Control Selectors */
#define MUTE_CONTROL                    0x01
#define VOLUME_CONTROL                  0x02

/* see audio.h for the rest, which is identical to v1 */
#define UAC2_FU_INPUT_GAIN		0x0b
#define UAC2_FU_INPUT_GAIN_PAD		0x0c
#define UAC2_FU_PHASE_INVERTER		0x0d
#define UAC2_FU_UNDERFLOW		0x0e
#define UAC2_FU_OVERFLOW		0x0f
#define UAC2_FU_LATENCY			0x10

/* A.17.8.1 Parametric Equalizer Section Effect Unit Control Selectors */
#define UAC2_PE_UNDEFINED		0x00
#define UAC2_PE_ENABLE			0x01
#define UAC2_PE_CENTERFREQ		0x02
#define UAC2_PE_QFACTOR			0x03
#define UAC2_PE_GAIN			0x04
#define UAC2_PE_UNDERFLOW		0x05
#define UAC2_PE_OVERFLOW		0x06
#define UAC2_PE_LATENCY			0x07

/* A.17.8.2 Reverberation Effect Unit Control Selectors */
#define UAC2_RV_UNDEFINED		0x00
#define UAC2_RV_ENABLE			0x01
#define UAC2_RV_TYPE			0x02
#define UAC2_RV_LEVEL			0x03
#define UAC2_RV_TIME			0x04
#define UAC2_RV_FEEDBACK		0x05
#define UAC2_RV_PREDELAY		0x06
#define UAC2_RV_DENSITY			0x07
#define UAC2_RV_HIFREQ_ROLLOFF		0x08
#define UAC2_RV_UNDERFLOW		0x09
#define UAC2_RV_OVERFLOW		0x0a
#define UAC2_RV_LATENCY			0x0b

/* A.17.8.3 Modulation Delay Effect Control Selectors */
#define UAC2_MD_UNDEFINED		0x00
#define UAC2_MD_ENABLE			0x01
#define UAC2_MD_BALANCE			0x02
#define UAC2_MD_RATE			0x03
#define UAC2_MD_DEPTH			0x04
#define UAC2_MD_TIME			0x05
#define UAC2_MD_FEEDBACK		0x06
#define UAC2_MD_UNDERFLOW		0x07
#define UAC2_MD_OVERFLOW		0x08
#define UAC2_MD_LATENCY			0x09

/* A.17.8.4 Dynamic Range Compressor Effect Unit Control Selectors */
#define UAC2_DR_UNDEFINED		0x00
#define UAC2_DR_ENABLE			0x01
#define UAC2_DR_COMPRESSION_RATE	0x02
#define UAC2_DR_MAXAMPL			0x03
#define UAC2_DR_THRESHOLD		0x04
#define UAC2_DR_ATTACK_TIME		0x05
#define UAC2_DR_RELEASE_TIME		0x06
#define UAC2_DR_UNDEFLOW		0x07
#define UAC2_DR_OVERFLOW		0x08
#define UAC2_DR_LATENCY			0x09

/* A.17.9.1 Up/Down-mix Processing Unit Control Selectors */
#define UAC2_UD_UNDEFINED		0x00
#define UAC2_UD_ENABLE			0x01
#define UAC2_UD_MODE_SELECT		0x02
#define UAC2_UD_CLUSTER			0x03
#define UAC2_UD_UNDERFLOW		0x04
#define UAC2_UD_OVERFLOW		0x05
#define UAC2_UD_LATENCY			0x06

/* A.17.9.2 Dolby Prologic[tm] Processing Unit Control Selectors */
#define UAC2_DP_UNDEFINED		0x00
#define UAC2_DP_ENABLE			0x01
#define UAC2_DP_MODE_SELECT		0x02
#define UAC2_DP_CLUSTER			0x03
#define UAC2_DP_UNDERFFLOW		0x04
#define UAC2_DP_OVERFLOW		0x05
#define UAC2_DP_LATENCY			0x06

/* A.17.9.3 Stereo Expander Processing Unit Control Selectors */
#define UAC2_ST_EXT_UNDEFINED		0x00
#define UAC2_ST_EXT_ENABLE		0x01
#define UAC2_ST_EXT_WIDTH		0x02
#define UAC2_ST_EXT_UNDEFLOW		0x03
#define UAC2_ST_EXT_OVERFLOW		0x04
#define UAC2_ST_EXT_LATENCY		0x05

/* A.17.10 Extension Unit Control Selectors */
#define UAC2_XU_UNDEFINED		0x00
#define UAC2_XU_ENABLE			0x01
#define UAC2_XU_CLUSTER			0x02
#define UAC2_XU_UNDERFLOW		0x03
#define UAC2_XU_OVERFLOW		0x04
#define UAC2_XU_LATENCY			0x05

/* A.17.11 AudioStreaming Interface Control Selectors */
#define UAC2_AS_UNDEFINED		0x00
#define UAC2_AS_ACT_ALT_SETTING		0x01
#define UAC2_AS_VAL_ALT_SETTINGS	0x02
#define UAC2_AS_AUDIO_DATA_FORMAT	0x03

/* A.17.12 Encoder Control Selectors */
#define UAC2_EN_UNDEFINED		0x00
#define UAC2_EN_BIT_RATE		0x01
#define UAC2_EN_QUALITY			0x02
#define UAC2_EN_VBR			0x03
#define UAC2_EN_TYPE			0x04
#define UAC2_EN_UNDERFLOW		0x05
#define UAC2_EN_OVERFLOW		0x06
#define UAC2_EN_ENCODER_ERROR		0x07
#define UAC2_EN_PARAM1			0x08
#define UAC2_EN_PARAM2			0x09
#define UAC2_EN_PARAM3			0x0a
#define UAC2_EN_PARAM4			0x0b
#define UAC2_EN_PARAM5			0x0c
#define UAC2_EN_PARAM6			0x0d
#define UAC2_EN_PARAM7			0x0e
#define UAC2_EN_PARAM8			0x0f

/* A.17.13.1 MPEG Decoder Control Selectors */
#define UAC2_MPEG_UNDEFINED		0x00
#define UAC2_MPEG_DUAL_CHANNEL		0x01
#define UAC2_MPEG_SECOND_STEREO		0x02
#define UAC2_MPEG_MULTILINGUAL		0x03
#define UAC2_MPEG_DYN_RANGE		0x04
#define UAC2_MPEG_SCALING		0x05
#define UAC2_MPEG_HILO_SCALING		0x06
#define UAC2_MPEG_UNDERFLOW		0x07
#define UAC2_MPEG_OVERFLOW		0x08
#define UAC2_MPEG_DECODER_ERROR		0x09

/* A17.13.2 AC3 Decoder Control Selectors */
#define UAC2_AC3_UNDEFINED		0x00
#define UAC2_AC3_MODE			0x01
#define UAC2_AC3_DYN_RANGE		0x02
#define UAC2_AC3_SCALING		0x03
#define UAC2_AC3_HILO_SCALING		0x04
#define UAC2_AC3_UNDERFLOW		0x05
#define UAC2_AC3_OVERFLOW		0x06
#define UAC2_AC3_DECODER_ERROR		0x07

/* A17.13.3 WMA Decoder Control Selectors */
#define UAC2_WMA_UNDEFINED		0x00
#define UAC2_WMA_UNDERFLOW		0x01
#define UAC2_WMA_OVERFLOW		0x02
#define UAC2_WMA_DECODER_ERROR		0x03

/* A17.13.4 DTS Decoder Control Selectors */
#define UAC2_DTS_UNDEFINED		0x00
#define UAC2_DTS_UNDERFLOW		0x01
#define UAC2_DTS_OVERFLOW		0x02
#define UAC2_DTS_DECODER_ERROR		0x03

/* A17.14 Endpoint Control Selectors */
#define UAC2_EP_CS_UNDEFINED		0x00
#define UAC2_EP_CS_PITCH		0x01
#define UAC2_EP_CS_DATA_OVERRUN		0x02
#define UAC2_EP_CS_DATA_UNDERRUN	0x03

#endif /* __USB_AUDIO_V2_H */

#endif /* __USB_AUDIO_H */

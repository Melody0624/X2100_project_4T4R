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


#endif /* __USB_AUDIO_H */

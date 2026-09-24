UNUSUAL_DEV(0x03ee, 0x6901, 0x0000, 0x0100,
		"Mitsumi",
		"USB FDD",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_SINGLE_LUN),

UNUSUAL_DEV(0x03f0, 0x0107, 0x0200, 0x0200, 
		"HP",
		"CD-Writer+",
		US_SC_8070, US_PR_CB, 0), 

/* Deduced by Jonathan Woithe <jwoithe@physics.adelaide.edu.au>
 * Entry needed for flags: US_FL_FIX_INQUIRY because initial inquiry message
 * always fails and confuses drive.
 */
UNUSUAL_DEV(0x0411, 0x001c, 0x0113, 0x0113,
		"Buffalo",
		"DUB-P40G HDD",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_FIX_INQUIRY),

/* Patch submitted by Philipp Friedrich <philipp@void.at> */
UNUSUAL_DEV(0x0482, 0x0100, 0x0100, 0x0100,
		"Kyocera",
		"Finecam S3x",
		US_SC_8070, US_PR_CB, US_FL_FIX_INQUIRY),

/* Patch submitted by Philipp Friedrich <philipp@void.at> */
UNUSUAL_DEV(0x0482, 0x0101, 0x0100, 0x0100,
		"Kyocera",
		"Finecam S4",
		US_SC_8070, US_PR_CB, US_FL_FIX_INQUIRY),

/* Patch submitted by Stephane Galles <stephane.galles@free.fr> */
UNUSUAL_DEV(0x0482, 0x0103, 0x0100, 0x0100,
		"Kyocera",
		"Finecam S5",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_FIX_INQUIRY),

/* Patch for Kyocera Finecam L3
 * Submitted by Michael Krauth <michael.krauth@web.de>
 * and Alessandro Fracchetti <al.fracchetti@tin.it>
 */
UNUSUAL_DEV(0x0482, 0x0105, 0x0100, 0x0100,
		"Kyocera",
		"Finecam L3",
		US_SC_SCSI, US_PR_BULK, US_FL_FIX_INQUIRY),

/* Reported by Paul Stewart <stewart@wetlogic.net>
 * This entry is needed because the device reports Sub=ff */
UNUSUAL_DEV(0x04a4, 0x0004, 0x0001, 0x0001,
		"Hitachi",
		"DVD-CAM DZ-MV100A Camcorder",
		US_SC_SCSI, US_PR_CB, US_FL_SINGLE_LUN),

/* BENQ DC5330
 * Reported by Manuel Fombuena <mfombuena@ya.com> and
 * Frank Copeland <fjc@thingy.apana.org.au> */
UNUSUAL_DEV(0x04a5, 0x3010, 0x0100, 0x0100,
		"Tekom Technologies, Inc",
		"300_CAMERA",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_IGNORE_RESIDUE),

/* Reported by Simon Levitt <simon@whattf.com>
 * This entry needs Sub and Proto fields */
UNUSUAL_DEV(0x04b8, 0x0601, 0x0100, 0x0100,
		"Epson",
		"875DC Storage",
		US_SC_SCSI, US_PR_CB, US_FL_FIX_INQUIRY),

/* Reported by Khalid Aziz <khalid@gonehiking.org>
 * This entry is needed because the device reports Sub=ff */
UNUSUAL_DEV(0x04b8, 0x0602, 0x0110, 0x0110,
		"Epson",
		"785EPX Storage",
		US_SC_SCSI, US_PR_BULK, US_FL_SINGLE_LUN),

/* Not sure who reported this originally but
 * Pavel Machek <pavel@ucw.cz> reported that the extra US_FL_SINGLE_LUN
 * flag be added */
UNUSUAL_DEV(0x04cb, 0x0100, 0x0000, 0x2210,
		"Fujifilm",
		"FinePix 1400Zoom",
		US_SC_UFI, US_PR_DEVICE, US_FL_FIX_INQUIRY | US_FL_SINGLE_LUN),

/* Reported by Peter W�chtler <pwaechtler@loewe-komp.de>
 * The device needs the flags only.
 */
UNUSUAL_DEV(0x04ce, 0x0002, 0x0074, 0x0074,
		"ScanLogic",
		"SL11R-IDE",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_FIX_INQUIRY),

/* Reported by Kriston Fincher <kriston@airmail.net>
 * Patch submitted by Sean Millichamp <sean@bruenor.org>
 * This is to support the Panasonic PalmCam PV-SD4090
 * This entry is needed because the device reports Sub=ff 
 */
UNUSUAL_DEV(0x04da, 0x0901, 0x0100, 0x0200,
		"Panasonic",
		"LS-120 Camera",
		US_SC_UFI, US_PR_DEVICE, 0),

/* From Yukihiro Nakai, via zaitcev@yahoo.com.
 * This is needed for CB instead of CBI */
UNUSUAL_DEV(0x04da, 0x0d05, 0x0000, 0x0000,
		"Sharp CE-CW05",
		"CD-R/RW Drive",
		US_SC_8070, US_PR_CB, 0),

/* Most of the following entries were developed with the help of
 * Shuttle/SCM directly.
 */
UNUSUAL_DEV(0x04e6, 0x0001, 0x0200, 0x0200, 
		"Matshita",
		"LS-120",
		US_SC_8020, US_PR_CB, 0),

/* Reported by Markus Demleitner <msdemlei@cl.uni-heidelberg.de> */
UNUSUAL_DEV(0x04e6, 0x0006, 0x0100, 0x0100, 
		"SCM Microsystems Inc.",
		"eUSB MMC Adapter",
		US_SC_SCSI, US_PR_CB, US_FL_SINGLE_LUN), 

/* Reported by Daniel Nouri <dpunktnpunkt@web.de> */
UNUSUAL_DEV(0x04e6, 0x0006, 0x0205, 0x0205, 
		"Shuttle",
		"eUSB MMC Adapter",
		US_SC_SCSI, US_PR_DEVICE, US_FL_SINGLE_LUN), 

UNUSUAL_DEV(0x04e6, 0x0007, 0x0100, 0x0200, 
		"Sony",
		"Hifd",
		US_SC_SCSI, US_PR_CB, US_FL_SINGLE_LUN), 

UNUSUAL_DEV(0x04e6, 0x0009, 0x0200, 0x0200, 
		"Shuttle",
		"eUSB ATA/ATAPI Adapter",
		US_SC_8020, US_PR_CB, 0),

UNUSUAL_DEV(0x04e6, 0x000a, 0x0200, 0x0200, 
		"Shuttle",
		"eUSB CompactFlash Adapter",
		US_SC_8020, US_PR_CB, 0),

UNUSUAL_DEV(0x04e6, 0x0101, 0x0200, 0x0200, 
		"Shuttle",
		"CD-RW Device",
		US_SC_8020, US_PR_CB, 0),

/* Iomega Clik! Drive 
 * Reported by David Chatenay <dchatenay@hotmail.com>
 * The reason this is needed is not fully known.
 */
UNUSUAL_DEV(0x0525, 0xa140, 0x0100, 0x0100,
		"Iomega",
		"USB Clik! 40",
		US_SC_8070, US_PR_BULK, US_FL_FIX_INQUIRY),

/* Yakumo Mega Image 37
 * Submitted by Stephan Fuhrmann <atomenergie@t-online.de> */
UNUSUAL_DEV(0x052b, 0x1801, 0x0100, 0x0100,
		"Tekom Technologies, Inc",
		"300_CAMERA",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_IGNORE_RESIDUE),

/* Another Yakumo camera.
 * Reported by Michele Alzetta <michele.alzetta@aliceposta.it> */
UNUSUAL_DEV(0x052b, 0x1804, 0x0100, 0x0100,
		"Tekom Technologies, Inc",
		"300_CAMERA",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_IGNORE_RESIDUE),

/* Reported by Iacopo Spalletti <avvisi@spalletti.it> */
UNUSUAL_DEV(0x052b, 0x1807, 0x0100, 0x0100,
		"Tekom Technologies, Inc",
		"300_CAMERA",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_IGNORE_RESIDUE),

/* Yakumo Mega Image 47
 * Reported by Bjoern Paetzel <kolrabi@kolrabi.de> */
UNUSUAL_DEV(0x052b, 0x1905, 0x0100, 0x0100,
		"Tekom Technologies, Inc",
		"400_CAMERA",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_IGNORE_RESIDUE),

/* Reported by Paul Ortyl <ortylp@3miasto.net>
 * Note that it's similar to the device above, only different prodID */
UNUSUAL_DEV(0x052b, 0x1911, 0x0100, 0x0100,
		"Tekom Technologies, Inc",
		"400_CAMERA",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_IGNORE_RESIDUE),

/* This entry is needed because the device reports Sub=ff */
UNUSUAL_DEV(0x054c, 0x0010, 0x0500, 0x0500, 
		"Sony",
		"DSC-T1", 
		US_SC_8070, US_PR_DEVICE, US_FL_SINGLE_LUN),


/* Reported by wim@geeks.nl */
UNUSUAL_DEV(0x054c, 0x0025, 0x0100, 0x0100, 
		"Sony",
		"Memorystick NW-MS7",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_SINGLE_LUN),

/* Submitted by Olaf Hering, <olh@suse.de> SuSE Bugzilla #49049 */
UNUSUAL_DEV(0x054c, 0x002c, 0x0501, 0x0501,
		"Sony",
		"USB Floppy Drive",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_SINGLE_LUN),

UNUSUAL_DEV(0x054c, 0x002d, 0x0100, 0x0100, 
		"Sony",
		"Memorystick MSAC-US1",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_SINGLE_LUN),

/* Submitted by Klaus Mueller <k.mueller@intershop.de> */
UNUSUAL_DEV(0x054c, 0x002e, 0x0106, 0x0310, 
		"Sony",
		"Handycam",
		US_SC_SCSI, US_PR_DEVICE, US_FL_SINGLE_LUN),

/* Submitted by Rajesh Kumble Nayak <nayak@obs-nice.fr> */
UNUSUAL_DEV(0x054c, 0x002e, 0x0500, 0x0500, 
		"Sony",
		"Handycam HC-85",
		US_SC_UFI, US_PR_DEVICE, US_FL_SINGLE_LUN),

UNUSUAL_DEV(0x054c, 0x0032, 0x0000, 0x9999,
		"Sony",
		"Memorystick MSC-U01N",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_SINGLE_LUN),

/* Submitted by Michal Mlotek <mlotek@foobar.pl> */
UNUSUAL_DEV(0x054c, 0x0058, 0x0000, 0x9999,
		"Sony",
		"PEG N760c Memorystick",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_FIX_INQUIRY),

UNUSUAL_DEV(0x054c, 0x0069, 0x0000, 0x9999,
		"Sony",
		"Memorystick MSC-U03",
		US_SC_UFI, US_PR_CB, US_FL_SINGLE_LUN),

/* Submitted by Nathan Babb <nathan@lexi.com> */
UNUSUAL_DEV(0x054c, 0x006d, 0x0000, 0x9999,
		"Sony",
		"PEG Mass Storage",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_FIX_INQUIRY),

/* Submitted by Mike Alborn <malborn@deandra.homeip.net> */
UNUSUAL_DEV(0x054c, 0x016a, 0x0000, 0x9999,
		"Sony",
		"PEG Mass Storage",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_FIX_INQUIRY),

/* Submitted by Frank Engel <frankie@cse.unsw.edu.au> */
UNUSUAL_DEV(0x054c, 0x0099, 0x0000, 0x9999,
		"Sony",
		"PEG Mass Storage",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_FIX_INQUIRY),

UNUSUAL_DEV(0x057b, 0x0000, 0x0000, 0x0299, 
		"Y-E Data",
		"Flashbuster-U",
		US_SC_DEVICE,  US_PR_CB, US_FL_SINGLE_LUN),

UNUSUAL_DEV(0x057b, 0x0000, 0x0300, 0x9999, 
		"Y-E Data",
		"Flashbuster-U",
		US_SC_DEVICE,  US_PR_DEVICE, US_FL_SINGLE_LUN),

/* Reported by Johann Cardon <johann.cardon@free.fr>
 * This entry is needed only because the device reports
 * bInterfaceClass = 0xff (vendor-specific)
 */
UNUSUAL_DEV(0x057b, 0x0022, 0x0000, 0x9999, 
		"Y-E Data",
		"Silicon Media R/W",
		US_SC_DEVICE, US_PR_DEVICE, 0),

/* Fabrizio Fellini <fello@libero.it> */
UNUSUAL_DEV(0x0595, 0x4343, 0x0000, 0x2210,
		"Fujifilm",
		"Digital Camera EX-20 DSC",
		US_SC_8070, US_PR_DEVICE, 0),

/* The entry was here before I took over, and had US_SC_RBC. It turns
 * out that isn't needed. Additionally, Torsten Eriksson
 * <Torsten.Eriksson@bergianska.se> is able to use his device fine
 * without this entry at all - but I don't suspect that will be true
 * for all users (the protocol is likely needed), so is staying at
 * this time. - Phil Dibowitz <phil@ipom.com>
 */
UNUSUAL_DEV(0x059f, 0xa601, 0x0200, 0x0200, 
		"LaCie",
		"USB Hard Disk",
		US_SC_DEVICE, US_PR_CB, 0),

/* Reported by Blake Matheny <bmatheny@purdue.edu> */
UNUSUAL_DEV(0x05dc, 0xb002, 0x0000, 0x0113,
		"Lexar",
		"USB CF Reader",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_FIX_INQUIRY),

/* Reported by Hanno Boeck <hanno@gmx.de>
 * Taken from the Lycoris Kernel */
UNUSUAL_DEV(0x0636, 0x0003, 0x0000, 0x9999,
		"Vivitar",
		"Vivicam 35Xx",
		US_SC_SCSI, US_PR_BULK, US_FL_FIX_INQUIRY),

UNUSUAL_DEV(0x0644, 0x0000, 0x0100, 0x0100, 
		"TEAC",
		"Floppy Drive",
		US_SC_UFI, US_PR_CB, 0), 

/* Submitted by Benny Sjostrand <benny@hostmobility.com> */
UNUSUAL_DEV( 0x0686, 0x4011, 0x0001, 0x0001,
		"Minolta",
		"Dimage F300",
		US_SC_SCSI, US_PR_BULK, 0),

/* Reported by Miguel A. Fosas <amn3s1a@ono.com> */
UNUSUAL_DEV(0x0686, 0x4017, 0x0001, 0x0001,
		"Minolta",
		"DIMAGE E223",
		US_SC_SCSI, US_PR_DEVICE, 0),

UNUSUAL_DEV(0x0693, 0x0002, 0x0100, 0x0100, 
		"Hagiwara",
		"FlashGate SmartMedia",
		US_SC_SCSI, US_PR_BULK, 0),

UNUSUAL_DEV(0x0693, 0x0005, 0x0100, 0x0100,
		"Hagiwara",
		"Flashgate",
		US_SC_SCSI, US_PR_BULK, 0), 

UNUSUAL_DEV(0x0781, 0x0001, 0x0200, 0x0200, 
		"Sandisk",
		"ImageMate SDDR-05a",
		US_SC_SCSI, US_PR_CB, US_FL_SINGLE_LUN),

UNUSUAL_DEV(0x0781, 0x0100, 0x0100, 0x0100,
		"Sandisk",
		"ImageMate SDDR-12",
		US_SC_SCSI, US_PR_CB, US_FL_SINGLE_LUN),

/* Datafab KECF-USB / Sagatek DCS-CF / Simpletech Flashlink UCF-100
 * Only revision 1.13 tested (same for all of the above devices,
 * based on the Datafab DF-UG-07 chip).  Needed for US_FL_FIX_INQUIRY.
 * Submitted by Marek Michalkiewicz <marekm@amelek.gda.pl>.
 * See also http://martin.wilck.bei.t-online.de/#kecf .
 */
UNUSUAL_DEV(0x07c4, 0xa400, 0x0000, 0xffff,
		"Datafab",
		"KECF-USB",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_FIX_INQUIRY),

/* Submitted by Hartmut Wahl <hwahl@hwahl.de>*/
UNUSUAL_DEV( 0x0839, 0x000a, 0x0001, 0x0001,
		"Samsung",
		"Digimax 410",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_FIX_INQUIRY),

/* Trumpion Microelectronics MP3 player (felipe_alfaro@linuxmail.org) */
UNUSUAL_DEV( 0x090a, 0x1200, 0x0000, 0x9999,
		"Trumpion",
		"MP3 player",
		US_SC_RBC, US_PR_BULK, 0),

/* This Pentax still camera is not conformant
 * to the USB storage specification: -
 * - It does not like the INQUIRY command. So we must handle this command
 *   of the SCSI layer ourselves.
 * Tested on Rev. 10.00 (0x1000)
 * Submitted by James Courtier-Dutton <James@superbug.demon.co.uk>
 */
UNUSUAL_DEV( 0x0a17, 0x0004, 0x1000, 0x1000,
		"Pentax",
		"Optio 2/3/400",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_FIX_INQUIRY),

/* Submitted by Per Winkvist <per.winkvist@uk.com> */
UNUSUAL_DEV( 0x0a17, 0x006, 0x0000, 0xffff,
		"Pentax",
		"Optio S/S4",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_FIX_INQUIRY),

/* Submitted by Joris Struyve <joris@struyve.be> */
UNUSUAL_DEV( 0x0d96, 0x410a, 0x0001, 0xffff,
		"Medion",
		"MD 7425",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_FIX_INQUIRY),

/*
 * Entry for Jenoptik JD 5200z3
 *
 * email: car.busse@gmx.de
 */
UNUSUAL_DEV(0x0d96, 0x5200, 0x0001, 0x0200,
		"Jenoptik",
		"JD 5200 z3",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_FIX_INQUIRY),

/* Reported by Lubomir Blaha <tritol@trilogic.cz>
 * I _REALLY_ don't know what 3rd, 4th number and all defines mean, but this
 * works for me. Can anybody correct these values? (I able to test corrected
 * version.)
 */
UNUSUAL_DEV( 0x0dd8, 0x1060, 0x0000, 0xffff,
		"Netac",
		"USB-CF-Card",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_FIX_INQUIRY),

/* Patch by Stephan Walter <stephan.walter@epfl.ch>
 * I don't know why, but it works... */
UNUSUAL_DEV( 0x0dda, 0x0001, 0x0012, 0x0012,
		"WINWARD",
		"Music Disk",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_IGNORE_RESIDUE),

/* Reported by Ian McConnell <ian at emit.demon.co.uk> */
UNUSUAL_DEV(0x0dda, 0x0301, 0x0012, 0x0012,
		"PNP_MP3",
		"PNP_MP3 PLAYER",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_IGNORE_RESIDUE),

/* Submitted by Antoine Mairesse <antoine.mairesse@free.fr> */
UNUSUAL_DEV( 0x0ed1, 0x6660, 0x0100, 0x0300,
		"USB",
		"Solid state disk",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_FIX_INQUIRY),

/* Submitted by Daniel Drake <dsd@gentoo.org>
 * Reported by dayul on the Gentoo Forums */
UNUSUAL_DEV(0x0ea0, 0x2168, 0x0110, 0x0110,
		"Ours Technology",
		"Flash Disk",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_IGNORE_RESIDUE),

/* Reported by Rastislav Stanik <rs_kernel@yahoo.com> */
UNUSUAL_DEV(0x0ea0, 0x6828, 0x0110, 0x0110,
		"USB",
		"Flash Disk",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_IGNORE_RESIDUE),

/* Reported by Kotrla Vitezslav <kotrla@ceb.cz> */
UNUSUAL_DEV(0x1370, 0x6828, 0x0110, 0x0110,
		"SWISSBIT",
		"Black Silver",
		US_SC_DEVICE, US_PR_DEVICE, US_FL_IGNORE_RESIDUE),


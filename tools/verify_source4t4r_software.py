"""Validate software-stage 4TX build without claiming RF readiness."""
from pathlib import Path
import hashlib,json,math,re,struct,subprocess,sys
from verify_vendor2t4r_tx01_cal import symbol_bytes
from verify_vendor2t4r_2M_ota import check_image

source,build,out,baseline=map(Path,sys.argv[1:])
snapshot=json.loads((out/'source_snapshot.json').read_text())
for name,digest in snapshot.items():
    assert hashlib.sha256((source/name).read_bytes()).hexdigest()==digest,name
base_commit='710a53b5618f493db033dab331409d34bc74a435'
def baseline_bytes(name):
    # The primary B SDK may advance independently; compare against the actual
    # four-TX adaptation base, rather than its current two-TX working files.
    return subprocess.check_output(['git','-C',str(baseline),'show',
        base_commit+':X2100_project-main/firmware/x2100/freertos/'+name])
core='vendor/motor_cycle_demo/src/'
unchanged=[core+'detection/'+n for n in ('cfar_detection.c','cfar_func_v2.c','angle_estimation_v1.c','detection_post_processing.c')]
unchanged += [core+n for n in ('algorithm_functions.c','rcs_estimation.c')]
unchanged += ['vendor/mmw_msg_pkt/mmw_msg_pkt.c','vendor/mmw_msg_pkt/mmw_msg_pkt.h',
              'vendor/InitPhaseCalib/zero_phase_correction.c','vendor/InitPhaseCalib/mapping_gen_processing.c']
for name in unchanged:
    assert (source/name).read_bytes().replace(b'\r\n',b'\n')==baseline_bytes(name).replace(b'\r\n',b'\n'),name
name=core+'detection/detection_processing.c'
def without_guard(text):
    return re.sub(r'#if NUM_SUBFRAMES != 1[^\n]*\n#error[^\n]*\n#endif','',text)
assert without_guard((source/name).read_text())==without_guard(baseline_bytes(name).decode().replace('\r\n','\n')),'DDMA/detection algorithm changed'
elf=(out/'zero.elf').read_bytes()
for name in ('vendor_init','detection_processing','detection_processing_v2','tracking_processing',
             'getEgoVlcByStatic','rcs_calculate','rcs_process_frame','InitPhaseCalibProcess',
             'mappingGen_processing_with_spacing'):
    assert symbol_bytes(elf,name),name
expected=(1,0,-.935064,.398385,-.697341,-.786336,1.191529,-.129519,
          -.091294,1.004782,-.278997,-.996838,.881429,-.629665,-.049072,1.230434,
          .497096,.775442,-.784077,-.567465,.282913,-.929807,.674274,.901324,
          .022635,-1.420770,.505457,1.368615,-1.150743,.940266,-.063695,-1.674663)
assert symbol_bytes(elf,'angCalibMat')==struct.pack('<32f',*expected)
spacing=-.00196*76.5e9/299792458
positions=struct.unpack('<16f',symbol_bytes(elf,'vxPosn_sf1_azm'))
assert all(abs(x-i*spacing)<2e-6 for i,x in enumerate(positions))
angles=struct.unpack('<128f',symbol_bytes(elf,'angFFT_interp'))
for i,x in enumerate(angles):
    assert abs(x-math.degrees(math.asin((i if i<64 else i-128)/(128*spacing))))<1e-5
header=(source/'vendor/motor_cycle_demo/inc/radar_types.h').read_text()
rf_ready=int(re.search(r'^#define SOURCE_4T4R_RF_READY ([01])$',header,re.M).group(1))
nm=baseline/'MotorCycle_Ingenic/MotorCycle_Ingenic/tools/toolchains/mips-xburst2-newlib430/bin/mips-sde-elf-nm'
names=subprocess.check_output([str(nm),str(out/'zero.elf')],text=True)
ready_symbol=re.findall(r'\b(rf_ready(?:\.\d+)?)$',names,re.M)
assert len(ready_symbol)==1
assert symbol_bytes(elf,ready_symbol[0])==bytes([rf_ready]),'Linked RF startup flag differs'
from test_4t4r_rf_slope import rows
rf_source=(source/'vendor/cheetah/source4t4r_register_config.h').read_text()
rf_rows=rows(rf_source,1,0)
rf_expected=b''.join(struct.pack('<HhH2x64I',chip,addr,count,*(words+[0]*(64-count)))
                     for chip,addr,count,words in rf_rows)
assert symbol_bytes(elf,'cheetah_default_config')==rf_expected,'Linked four-TX register table mismatch'
image_name='zero_ota.bin' if rf_ready else 'zero_software_only.bin'
image=check_image(out/image_name)
metadata={'base_commit':'710a53b5618f493db033dab331409d34bc74a435','source_sdk':str(source),
          'build_sdk':str(build),'rf_ready':bool(rf_ready),'flashable_package':False,
          'tx':4,'rx':4,'virtual_channels':16,'adc_samples':506,'chirps':128,
          'sample_rate_hz':26665000,'center_hz':76500000000,'slope_hz_per_s':16699000000000,
          'chirp_us':26,'frame_us':50328,'ddma_bands':8,'bins_per_band':16,
          'matrix_source':'Record_20260928_202805_adc.dat, range bin 5, TX-major',
          'matrix_verified_new_waveform':False,'algorithm_files_unchanged':unchanged,
          'rtos_bytes':len(image),'rtos_sha256':hashlib.sha256(image).hexdigest()}
metadata.update(rf_table_imported=True,rf_source='vendor_bsis(2).c, cheetah_128_512_config',
                rf_source_sha256='f3d34a8b4a43cadea04f6b4e1ebc5bd0fcfe1cf83f150b06c2f0972d7cac58b6',
                fstep=171,register_slope_hz_per_s=16699218750000,
                rf_rows=len(rf_rows),hardware_verified_new_slope=False)
metadata.update(image_file=image_name,experimental_live=bool(rf_ready))
(out/'SOFTWARE_BUILD_MANIFEST.json').write_text(json.dumps(metadata,indent=2)+'\n')
print('PASS: 16-channel defaults/positions, theoretical angles, original algorithms/protocol, linked RF table and startup flag; experimental live='+str(bool(rf_ready)))

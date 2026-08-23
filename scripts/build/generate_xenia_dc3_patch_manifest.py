#!/usr/bin/env python3
"""
Generate a machine-readable DC3 patch manifest for Xenia's title-specific
NUI/XBC resolver.

This is a post-link artifact derived from:
  - the linked PE (for .text fingerprint / section range)
  - config/373307D9/symbols.txt (for semantic target addresses)

Output: JSON manifest (default: build/373307D9/xenia_dc3_patch_manifest.json)

Fingerprint notes:
  - pe.text.fnv1a64 is the static PE/.text hash used for artifact identity.
  - pe.text.xenia_runtime_fnv1a64 is optional and should be populated from a
    Xenia runtime log if exact runtime-layout matching is desired.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path
from typing import Dict, Optional, Tuple

ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_PE = ROOT / "build" / "373307D9" / "default.exe"
DEFAULT_SYMBOLS = ROOT / "config" / "373307D9" / "symbols.txt"
DEFAULT_MAP = ROOT / "build" / "373307D9" / "default.map"
DEFAULT_PE_MAP = ROOT / "build" / "373307D9" / "default.exe.MAP"
DEFAULT_OUT = ROOT / "build" / "373307D9" / "xenia_dc3_patch_manifest.json"

FNV64_OFFSET = 0xCBF29CE484222325
FNV64_PRIME = 0x100000001B3

SYMBOL_RE = re.compile(
    r"^(?P<name>.+?)\s*=\s*(?P<section>\.[A-Za-z0-9_$]+):0x(?P<addr>[0-9A-Fa-f]+);"
    r"(?:\s*//\s*(?P<meta>.*))?$"
)
SIZE_RE = re.compile(r"\bsize:0x([0-9A-Fa-f]+)\b")
TYPE_RE = re.compile(r"\btype:([A-Za-z_]+)\b")
MAP_PUBLIC_RE = re.compile(
    r"^\s*(?P<seg>[0-9A-Fa-f]{4}):(?P<off>[0-9A-Fa-f]{8})\s+"
    r"(?P<name>\S+)\s+(?P<abs>[0-9A-Fa-f]{8})\b"
    r"(?:\s+[fi ]+\s+(?P<obj>\S+\.obj))?"
)

CRT_SENTINELS = {"__xc_a", "__xc_z", "__xi_a", "__xi_z"}

# Address catalog: kAddr field name -> MAP symbol name.
# These populate the Dc3Addresses struct at runtime so that decomp XEX
# rebuilds don't require manual address updates in dc3_hack_pack.cc.
ADDRESS_CATALOG = {
    # CRT functions
    "ioinit":                    "_ioinit",
    "cinit":                     "_cinit",
    "errno_fn":                  "_errno",
    "invalid_parameter_noinfo":  "_invalid_parameter_noinfo",
    "call_reportfault":          "_call_reportfault",
    "amsg_exit":                 "_amsg_exit",
    "report_gsfailure":          "__report_gsfailure",
    # CRT formatter
    "output_l":                  "_output_l",
    "woutput_l":                 "_woutput_l",
    # Debug subsystem
    "debug_print":               "?Print@Debug@@UAAXPBD@Z",
    "debug_fail":                "?Fail@Debug@@QAAXPBDPAX@Z",
    "debug_do_crucible":         "?DoCrucible@Debug@@QAAXW4ModalType@1@PBDPAX@Z",
    "datanode_print":            "?Print@DataNode@@QBAXAAVTextStream@@_NH@Z",
    # Import/thunk
    "xapi_call_thread_notify":   "XapiCallThreadNotifyRoutines",
    "xregister_thread_notify":   "XRegisterThreadNotifyRoutine",
    "mtinit":                    "_mtinit",
    # Locale
    "get_system_language":       "?GetSystemLanguage@@YA?AVSymbol@@V1@@Z",
    "get_system_locale":         "?GetSystemLocale@@YA?AVSymbol@@V1@@Z",
    "xget_locale":               "XGetLocale",
    "xtl_get_language":          "XTLGetLanguage",
    "debug_break":               "DebugBreak",
    # ReadCacheStream probes
    "rcs_read_cache_stream":     "?ReadCacheStream@@YAPAVDataArray@@AAVBinStream@@PBD@Z",
    "rcs_bufstream_read_impl":   "?ReadImpl@BufStream@@EAAXPAXH@Z",
    "rcs_bufstream_seek_impl":   "?SeekImpl@BufStream@@EAAXHW4SeekType@BinStream@@@Z",
    # SystemConfig / FindArray
    "system_config_2":           "?SystemConfig@@YAPAVDataArray@@VSymbol@@0@Z",
    "find_array":                "?FindArray@DataArray@@QBAPAV1@VSymbol@@_N@Z",
    # Object / factory globals
    "object_factories_map":      "?sFactories@Object@Hmx@@0V?$map@VSymbol@@P6APAVObject@Hmx@@XZU?$less@VSymbol@@@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@$$CBVSymbol@@P6APAVObject@Hmx@@XZ@stlpmtx_std@@@5@@stlpmtx_std@@A",
    "register_factory":          "?RegisterFactory@Object@Hmx@@SAXVSymbol@@P6APAV12@XZ@Z",
    "new_object":                "?NewObject@Object@Hmx@@SAPAV12@VSymbol@@@Z",
    "object_set_name":           "?SetName@Object@Hmx@@UAAXPBDPAVObjectDir@@@Z",
    "load_meta_materials":       "?LoadMetaMaterials@RndMat@@KAPAVObjectDir@@XZ",
    "rndmat_static_name_sym":    "?name@?1??StaticClassName@RndMat@@SA?AVSymbol@@XZ@4V3@A",
    "metamaterial_static_name_sym": "?name@?1??StaticClassName@MetaMaterial@@SA?AVSymbol@@XZ@4V3@A",
    "g_system_config":           "?gSystemConfig@@3PAVDataArray@@A",
    "read_system_config":        "?ReadSystemConfig@@YAPAVDataArray@@PBD@Z",
    "g_string_table_global":     "?gStringTable@@3PAVStringTable@@A",
    "g_chunk_alloc":             "?gChunkAlloc@@3PAVChunkAllocator@@A",
    # Memory / allocator
    "mem_or_pool_alloc":         "?MemOrPoolAlloc@@YAPAXHPBDH0@Z",
    "mem_alloc":                 "?MemAlloc@@YAPAXHPBDH0H@Z",
    "pool_alloc":                "?PoolAlloc@@YAPAXHHPBDH0@Z",
    "mem_free":                  "?MemFree@@YAXPAXPBDH1@Z",
    "pool_free":                 "?PoolFree@@YAXHPAXPBDH1@Z",
    "mem_or_pool_free":          "?MemOrPoolFree@@YAXHPAXPBDH1@Z",
    "operator_new":              "??2@YAPAXI@Z",
    "operator_delete":           "??3@YAXPAX@Z",
    "critsec_ctor":              "??0CriticalSection@@QAA@XZ",
    "critsec_enter":             "?Enter@CriticalSection@@QAAXXZ",
    "critsec_exit":              "?Exit@CriticalSection@@QAAXXZ",
    "g_num_heaps":               "?gNumHeaps@@3HA",
    "string_reserve":            "?reserve@String@@QAAXI@Z",
    # BinStream / Rand2
    "binstream_read":            "?Read@BinStream@@QAAXPAXH@Z",
    "binstream_read_endian":     "?ReadEndian@BinStream@@QAAXPAXH@Z",
    "rand2_ctor":                "??0Rand2@@QAA@H@Z",
    "rand2_int":                 "?Int@Rand2@@QAAHXZ",
    "bs_op_dataarray":           "??5@YAAAVBinStream@@AAV0@AAPAVDataArray@@@Z",
    # DataArray / DataNode / Symbol
    "string_table_add":          "?Add@StringTable@@QAAPBDPBD@Z",
    "symbol_preinit":            "?PreInit@Symbol@@SAXHH@Z",
    # TextStream / String ops
    "textstream_op_const_char":  "??6TextStream@@QAAAAV0@PBD@Z",
    "string_op_plus_eq":         "??YString@@QAAAAV0@PBD@Z",
    # XMP
    "xmp_override_bg_music":     "XMPOverrideBackgroundMusic",
    "xmp_restore_bg_music":      "XMPRestoreBackgroundMusic",
    # Write bridges
    "write_nolock":              "_write_nolock",
    "write_fn":                  "_write",
    # Holmes
    "protocol_debug_string":     "?ProtocolDebugString@Holmes@@YAPBDE@Z",
    # Wind
    "set_wind":                  "?SetWind@@YAXHHMMM@Z",
    # gConditional / gDataArrayConditional ctor (BSS addr extracted from PPC)
    "g_conditional_ctor":                "??__EgConditional@@YAXXZ",
    "g_data_array_conditional_ctor":     "??__EgDataArrayConditional@@YAXXZ",
    # FileIsLocal
    "file_is_local":             "?FileIsLocal@@YA_NPBD@Z",
    # File system globals
    "g_using_cd":                "?gUsingCD@@3HA",
    "check_for_archive":         "?CheckForArchive@?A0x4af72ae9@@YAXXZ",
    "file_init":                 "FileInit",
    "archive_init":              "?ArchiveInit@@YAXXZ",
    "the_archive":               "?TheArchive@@3PAVArchive@@A",
    "arkfile_read":              "?Read@ArkFile@@UAAHPAXH@Z",
    # Data section globals
    "g_null_str":                "?gNullStr@@3PBDB",
    "g_num_heaps":               "?gNumHeaps@@3HA",
    # STL container globals (BSS, sentinels init'd from host)
    "the_load_mgr":              "?TheLoadMgr@@3VLoadMgr@@A",
    "auto_timer_stmrs":          "?sTimers@AutoTimer@@0V?$list@U?$pair@VTimer@@VTimerStats@@@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@VTimer@@VTimerStats@@@stlpmtx_std@@@2@@stlpmtx_std@@A",
    "rnd_overlay_soverlays":     "?sOverlays@RndOverlay@@0V?$list@PAVRndOverlay@@V?$StlNodeAlloc@PAVRndOverlay@@@stlpmtx_std@@@stlpmtx_std@@A",
    "synth_pollable_spollables": "?sPollables@SynthPollable@@0V?$list@PAVSynthPollable@@V?$StlNodeAlloc@PAVSynthPollable@@@stlpmtx_std@@@stlpmtx_std@@A",
    "midi_parser_sparsers":      "?sParsers@MidiParser@@0V?$list@PAVMidiParser@@V?$StlNodeAlloc@PAVMidiParser@@@stlpmtx_std@@@stlpmtx_std@@A",
    "rnd_multi_mesh_sproxy":     "?sProxyPool@RndMultiMesh@@1V?$list@U?$pair@PAVRndMultiMeshProxy@@H@stlpmtx_std@@V?$StlNodeAlloc@U?$pair@PAVRndMultiMeshProxy@@H@stlpmtx_std@@@2@@stlpmtx_std@@A",
    "system_pre_init_1":         "?SystemPreInit@@YAXPBD@Z",
    "system_pre_init_2":         "?SystemPreInit@@YAXPBD0@Z",
    # RndTransformable
    "set_dirty_force":           "?SetDirty_Force@RndTransformable@@AAAXXZ",
    # Memory_Xbox
    "alloc_type":                "?AllocType@?A0x2be09a71@@YAPBDK@Z",
    # Rnd
    "rnd_create_defaults":       "?CreateDefaults@Rnd@@IAAXXZ",
    # MetaMaterial
    "create_and_set_meta_mat":   "?CreateAndSetMetaMat@@YAXPAVRndMat@@@Z",
    "s_meta_materials":          "?sMetaMaterials@RndMat@@1PAVObjectDir@@A",
    # Post-processing / GPU init
    "ng_postproc_rebuild_tex":   "?RebuildTex@NgPostProc@@SAXXZ",
    "ng_dofproc_init":           "?Init@NgDOFProc@@SAXXZ",
    "rnd_shadowmap_init":        "?Init@RndShadowMap@@SAXXZ",
    "dxrnd_suspend":             "?Suspend@DxRnd@@UAAXXZ",
    "occlusion_query_mgr_ctor":  "??0DxRndOcclusionQueryMgr@@QAA@XZ",
    "d3d_device_suspend":        "D3DDevice_Suspend",
    "d3d_device_resume":         "D3DDevice_Resume",
    "dxrnd_init_buffers":        "?InitBuffers@DxRnd@@AAAXXZ",
    "dxrnd_create_post_textures": "?CreatePostTextures@DxRnd@@AAAXXZ",
    # Audio / Synth
    "synth360_preinit":          "?PreInit@Synth360@@UAAXXZ",
    "synth_init":                "?SynthInit@@YAXXZ",
    # Bink video
    "bink_start_async_thread":   "?BinkStartAsyncThread@@YAHHH@Z",
    "bink_platform_init":        "?PlatformInit@BinkMovieSys@@QAAXXZ",
    # CRT RTTI
    "rt_dynamic_cast":           "__RTDynamicCast",
    # String constants
    "g_null_str":                "?gNullStr@@3PBDB",
    # SkeletonIdentifier (Kinect player identification)
    "skeleton_identifier_init":  "?Init@SkeletonIdentifier@@QAAXXZ",
    "skeleton_identifier_poll":  "?Poll@SkeletonIdentifier@@QAAXXZ",
    # OSCMessenger (Holmes debug networking)
    "osc_messenger_poll":        "?Poll@OSCMessenger@@QAAXXZ",
    # LoadMgr (async file I/O)
    "poll_front_loader":         "?PollFrontLoader@LoadMgr@@AAAXXZ",
    "poll_until_loaded":         "?PollUntilLoaded@LoadMgr@@QAAXPAVLoader@@0@Z",
    # File cache / decompression globals
    "g_caches":                  "?gCaches@@3V?$list@PAVFileCache@@V?$StlNodeAlloc@PAVFileCache@@@stlpmtx_std@@@stlpmtx_std@@A",
    "g_decompression_queue":     "?gDecompressionQueue@?A0x7ea4e606@@3V?$list@UDecompressTask@@V?$StlNodeAlloc@UDecompressTask@@@stlpmtx_std@@@stlpmtx_std@@A",
    # HamIKEffector (IK telemetry instrumentation)
    "ham_ik_poll":               "?Poll@HamIKEffector@@UAAXXZ",
    "ham_ik_apply_constraints":  "?ApplyConstraints@HamIKEffector@@IAAMAAVQuatXfm@@ABVTransform@@PAV1@@Z",
    "ham_ik_get_ground_height":  "?GetGroundHeight@HamIKEffector@@IAAMPAVRndTransformable@@@Z",
    "ham_ik_get_type":           "?GetType@HamIKEffector@@IAA?AW4EffectorType@1@XZ",
    "ham_ik_apply_pos_constraints": "?ApplyPosConstraints@HamIKEffector@@IAAMAAVVector3@@ABV2@PAV1@@Z",
    "ham_ik_elbow":              "?IKElbow@HamIKEffector@@IAAXABVVector3@@@Z",
    "ham_ik_do_fancy_elbow":     "?DoFancyElbow@HamIKEffector@@IAAXAAVQuatXfm@@M@Z",
    "holmes_client_poll":        "?HolmesClientPoll@@YAXXZ",
}

# Hack-pack stub targets: display_name -> MSVC mangled MAP symbol name
# These are functions that dc3_hack_pack.cc needs to stub at runtime.
# Addresses come from the MAP file instead of being hardcoded.
HACK_PACK_STUBS = {
    # Import/notification
    "XapiCallThreadNotifyRoutines": "XapiCallThreadNotifyRoutines",
    # CRT output
    "_output_l": "_output_l",
    "_woutput_l": "_woutput_l",
    # XMP
    "XMPOverrideBackgroundMusic": "XMPOverrideBackgroundMusic",
    "XMPRestoreBackgroundMusic": "XMPRestoreBackgroundMusic",
    # Debug
    "Debug::Fail": "?Fail@Debug@@QAAXPBDPAX@Z",
    "Rnd::SetupFont": "?SetupFont@Rnd@@IAAXXZ",
    "Rnd::PreInit": "?PreInit@Rnd@@UAAXXZ",
    "NgRnd::PreInit": "?PreInit@NgRnd@@UAAXXZ",
    "DxRnd::PreInit": "?PreInit@DxRnd@@QAAXPAUHWND__@@@Z",
    "FileIsLocal": "?FileIsLocal@@YA_NPBD@Z",
    "CheckForArchive": "?CheckForArchive@?A0x8038bdc3@@YAXXZ",
    "XGetLocale": "XGetLocale",
    "XTLGetLanguage": "XTLGetLanguage",
    "DebugBreak": "DebugBreak",
    "GetSystemLanguage": "?GetSystemLanguage@@YA?AVSymbol@@V1@@Z",
    "GetSystemLocale": "?GetSystemLocale@@YA?AVSymbol@@V1@@Z",
    "DataNode::Print": "?Print@DataNode@@QBAXAAVTextStream@@_NH@Z",
    "DataArray::AddRef": "?AddRef@DataArray@@QAAXXZ",
    "DataArray::Release": "?Release@DataArray@@QAAXXZ",
    "RndMat::CreateMetaMaterial": "?CreateMetaMaterial@RndMat@@QAAPAVMetaMaterial@@_N@Z",
    # Memory management
    "NtAllocateVirtualMemoryWrapper": "NtAllocateVirtualMemoryWrapper",
    "RtlpInsertUnCommittedPages": "RtlpInsertUnCommittedPages",
    # Holmes (debug network) - free functions, not HolmesClient:: members
    "HolmesClientInit": "?HolmesClientInit@@YAXXZ",
    "HolmesClientReInit": "?HolmesClientReInit@@YAXXZ",
    "HolmesClientPoll": "?HolmesClientPoll@@YAXXZ",
    "HolmesClientPollInternal": "?HolmesClientPollInternal@?A0x49b544a7@@YAX_N@Z",
    "HolmesClientInitOpcode": "?HolmesClientInitOpcode@@YA_N_N@Z",
    "HolmesClientTerminate": "?HolmesClientTerminate@@YAXXZ",
    "CanUseHolmes": "?CanUseHolmes@@YA_NH@Z",
    "UsingHolmes": "?UsingHolmes@@YA_NH@Z",
    "ProtocolDebugString": "?ProtocolDebugString@Holmes@@YAPBDE@Z",
    "HolmesSetFileShare": "?HolmesSetFileShare@@YAXPBD0@Z",
    "HolmesFileHostName": "?HolmesFileHostName@@YAPBDXZ",
    "HolmesFileShare": "?HolmesFileShare@@YAPBDXZ",
    "HolmesResolveIP": "?HolmesResolveIP@@YA?AVNetAddress@@XZ",
    "BeginCmd": "?BeginCmd@?A0x49b544a7@@YAXW4Protocol@Holmes@@_N@Z",
    "CheckForResponse": "?CheckForResponse@?A0x49b544a7@@YA_NW4Protocol@Holmes@@_N@Z",
    "WaitForAnyResponse": "?WaitForAnyResponse@?A0x49b544a7@@YAXW4Protocol@Holmes@@@Z",
    "EndCmd": "?EndCmd@?A0x49b544a7@@YAXW4Protocol@Holmes@@@Z",
    "CheckReads": "?CheckReads@?A0x49b544a7@@YA_N_N@Z",
    "CheckInput": "?CheckInput@?A0x49b544a7@@YAX_N@Z",
    "WaitForResponse": "?WaitForResponse@?A0x49b544a7@@YAXW4Protocol@Holmes@@@Z",
    "WaitForReads": "?WaitForReads@?A0x49b544a7@@YAXXZ",
    "HolmesClientPollKeyboard": "?HolmesClientPollKeyboard@@YAXXZ",
    "HolmesClientPollJoypad": "?HolmesClientPollJoypad@@YAIXZ",
    "HolmesClientOpen": "?HolmesClientOpen@@YA_NPBDHAAIAAH@Z",
    "HolmesClientRead": "?HolmesClientRead@@YAXHHHPAXPAVFile@@@Z",
    "HolmesClientReadDone": "?HolmesClientReadDone@@YA_NPAVFile@@@Z",
    "HolmesClientWrite": "?HolmesClientWrite@@YAXHHHPBX@Z",
    "HolmesClientTruncate": "?HolmesClientTruncate@@YAXHH@Z",
    "HolmesClientClose": "?HolmesClientClose@@YAXPAVFile@@H@Z",
    "HolmesClientGetStat": "?HolmesClientGetStat@@YAHPBDAAUFileStat@@@Z",
    "HolmesClientSysExec": "?HolmesClientSysExec@@YAHPBD@Z",
    "HolmesClientMkDir": "?HolmesClientMkDir@@YAHPBD@Z",
    "HolmesClientDelete": "?HolmesClientDelete@@YAHPBD@Z",
    "HolmesClientEnumerate": "?HolmesClientEnumerate@@YAXPBDP6AX00@Z_N02@Z",
    "HolmesClientCacheFile": "?HolmesClientCacheFile@@YA_NPADPBD@Z",
    "HolmesClientCacheResource": "?HolmesClientCacheResource@@YA?AW4CacheResourceResult@@PBD0@Z",
    "HolmesToLocal": "?HolmesToLocal@@YAXPADPBD@Z",
    "HolmesFlushStreamBuffer": "?HolmesFlushStreamBuffer@?A0x49b544a7@@YAXXZ",
    "DumpHolmesLog": "?DumpHolmesLog@@YA?AVDataNode@@PAVDataArray@@@Z",
    "HolmesClientStackTrace": "?HolmesClientStackTrace@@YAXPBDPAUStackData@@HAAVString@@@Z",
    "HolmesClientSendMessage": "?HolmesClientSendMessage@@YAXABVMessage@@@Z",
    # Splash screen / boot stubs
    "Splash::PrepareNext": "?PrepareNext@Splash@@QAA_NXZ",
    "Splash::BeginSplasher": "?BeginSplasher@Splash@@QAAXXZ",
    "Splash::Suspend": "?Suspend@Splash@@QAAXXZ",
    "Splash::Resume": "?Resume@Splash@@QAAXXZ",
    # Kinect / camera
    "LiveCameraInput::PreInit": "?PreInit@LiveCameraInput@@SAXXZ",
    "LiveCameraInput::Init": "?Init@LiveCameraInput@@SAXXZ",
    # Checksum
    "HasFileChecksumData": "?HasFileChecksumData@@YA_NXZ",
    # Voice / gesture / input
    "VoiceInputPanel::LoadVoiceContexts": "?LoadVoiceContexts@VoiceInputPanel@@AAAXXZ",
    "ShellInput::Init": "?Init@ShellInput@@QAAXXZ",
    # Audio / fader
    "Fader::UpdateValue": "?UpdateValue@Fader@@AAAXMMM@Z",
    # UI
    "UIScreen::HasPanel": "?HasPanel@UIScreen@@QAA_NPAVUIPanel@@@Z",
    # Move / choreography
    "MoveMgr::Init": "?Init@MoveMgr@@SAXPBD@Z",
    # Object system
    "ObjRef::ReplaceList": "?ReplaceList@ObjRef@@QAAXPAVObject@Hmx@@@Z",
    "list<ObjectDir*>::clear": "?clear@?$_List_base@PAVObjectDir@@V?$StlNodeAlloc@PAVObjectDir@@@stlpmtx_std@@@stlpmtx_std@@QAAXXZ",
    # String operations
    "String::operator+=": "??YString@@QAAAAV0@PBD@Z",
    # UI
    "UIManager::GotoFirstScreen": "?GotoFirstScreen@UIManager@@QAAXXZ",
    # DirLoader
    "ClassAndNameSort::ClassIndex": "?ClassIndex@ClassAndNameSort@DirLoader@@IAAHPAVObject@Hmx@@@Z",
    "ClassAndNameSort::operator()": "??RClassAndNameSort@DirLoader@@QAA_NPAVObject@Hmx@@0@Z",
    "DirLoader::SaveObjects": "?SaveObjects@DirLoader@@SAXAAVBinStream@@PAVObjectDir@@@Z",
    # Skeleton / Kinect per-frame
    "SkeletonUpdate::InstanceHandle": "?InstanceHandle@SkeletonUpdate@@SA?AVSkeletonUpdateHandle@@XZ",
    "SkeletonUpdate::PostUpdate": "?PostUpdate@SkeletonUpdate@@AAAXXZ",
    "SkeletonHistoryArchive::AddToHistory": "?AddToHistory@SkeletonHistoryArchive@@QAAXHABVSkeleton@@@Z",
    # Holmes debug networking
    "OSCMessenger::Poll": "?Poll@OSCMessenger@@QAAXXZ",
    # Skeleton identifier (Kinect)
    "SkeletonIdentifier::Init": "?Init@SkeletonIdentifier@@QAAXXZ",
    "SkeletonIdentifier::Poll": "?Poll@SkeletonIdentifier@@QAAXXZ",
    # Gesture manager (Kinect)
    "GestureMgr::Poll": "?Poll@GestureMgr@@QAAXXZ",
    "GestureMgr::GetSkeleton": "?GetSkeleton@GestureMgr@@QAAAAVSkeleton@@H@Z",
    "GestureMgr::UpdateTrackedSkeletons": "?UpdateTrackedSkeletons@GestureMgr@@QAAXXZ",
    # App rendering
    "App::DrawRegular": "?DrawRegular@App@@IAAXXZ",
    # CRT stopgaps
    "_errno": "_errno",
    "_invalid_parameter_noinfo": "_invalid_parameter_noinfo",
    # CRT injection
    "InitMakeString": "?InitMakeString@@YAXXZ",
    # Skeleton injection targets
    "NuiSkeletonGetNextFrame_addr": "NuiSkeletonGetNextFrame",
    "SkeletonUpdateThread": "?SkeletonUpdateThread@@YAKPAX@Z",
    "SkeletonUpdate::Update": "?Update@SkeletonUpdate@@AAAXXZ",
    # XAUDIO2 deadlock breakers (nop APU has no render thread to release CS)
    "CX2SourceVoice::Initialize": "?Initialize@CX2SourceVoice@XAUDIO2@@UAAJIMPBUtWAVEFORMATEX@@PBUXAUDIO2_VOICE_SENDS@@PBUXAUDIO2_EFFECT_CHAIN@@@Z",
    "CX2SourceVoice::Start": "?Start@CX2SourceVoice@XAUDIO2@@UAAJII@Z",
    "CX2SourceVoice::Stop": "?Stop@CX2SourceVoice@XAUDIO2@@UAAJII@Z",
    "CX2Engine::StartEngine": "?StartEngine@CX2Engine@XAUDIO2@@UAAJXZ",
    # I/O / archive stubs
    "CDReadDone": "?CDReadDone@@YA_NXZ",
    "ReadError": "?ReadError@@YAXPBD@Z",
    # Checksum data
    "SetFileChecksumData": "?SetFileChecksumData@@YAXPAUFileChecksum@@H@Z",
    # File cache
    "FileCache::GetFileAll": "?GetFileAll@FileCache@@SAPAVFile@@PBD@Z",
    # Character / animation
    "CharClip::Init": "?Init@CharClip@@SAXXZ",
    # Audio / security
    "Synth::InitSecurity": "?InitSecurity@Synth@@UAAXXZ",
    # Song manager
    "HamSongMgr::Init": "?Init@HamSongMgr@@UAAXXZ",
    # Locale
    "Locale::Init": "?Init@Locale@@QAAXXZ",
    # STL container stubs
    "list<RndTransformable*>::remove": "?remove@?$list@PAVRndTransformable@@V?$StlNodeAlloc@PAVRndTransformable@@@stlpmtx_std@@@stlpmtx_std@@QAAXABQAVRndTransformable@@@Z",
    # Flow / game state
    "FlowManager::Poll": "?Poll@FlowManager@@QAAXXZ",
    # Memory
    "MemInit": "?MemInit@@YAXXZ",
    "CriticalSection::CriticalSection": "??0CriticalSection@@QAA@XZ",
    "CriticalSection::Enter": "?Enter@CriticalSection@@QAAXXZ",
    "CriticalSection::Exit": "?Exit@CriticalSection@@QAAXXZ",
}


def fnv1a64(data: bytes) -> int:
    h = FNV64_OFFSET
    for b in data:
        h ^= b
        h = (h * FNV64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return h


def parse_pe_text_info(pe_data: bytes) -> Dict[str, int]:
    if pe_data[:2] != b"MZ":
        raise ValueError("not a PE (missing MZ)")
    pe_off = struct.unpack_from("<I", pe_data, 0x3C)[0]
    if pe_data[pe_off:pe_off + 4] != b"PE\x00\x00":
        raise ValueError("not a PE (missing PE signature)")

    num_sections = struct.unpack_from("<H", pe_data, pe_off + 6)[0]
    opt_size = struct.unpack_from("<H", pe_data, pe_off + 20)[0]
    opt_off = pe_off + 24
    image_base = struct.unpack_from("<I", pe_data, opt_off + 28)[0]
    sec_off = opt_off + opt_size

    for i in range(num_sections):
        off = sec_off + i * 40
        name = pe_data[off:off + 8].split(b"\x00", 1)[0]
        if name != b".text":
            continue
        vsize, vaddr, raw_size, raw_ptr = struct.unpack_from("<IIII", pe_data, off + 8)
        if raw_ptr + raw_size > len(pe_data):
            raise ValueError(".text raw range exceeds file size")
        if raw_size < vsize:
            # Rare, but pad to virtual size so the hash matches the runtime's
            # in-memory .text hashing semantics.
            text_blob = pe_data[raw_ptr:raw_ptr + raw_size] + b"\x00" * (vsize - raw_size)
        else:
            text_blob = pe_data[raw_ptr:raw_ptr + vsize]
        return {
            "image_base": image_base,
            "rva": vaddr,
            "size": vsize,
            "address": image_base + vaddr,
            "fnv1a64": fnv1a64(text_blob),
            "raw_size": raw_size,
        }

    raise ValueError(".text section not found")


def parse_pe_section_info(pe_data: bytes) -> Dict[str, Dict[str, int]]:
    """Parse all PE section headers, returning {name: {address, size}} dicts."""
    if pe_data[:2] != b"MZ":
        raise ValueError("not a PE (missing MZ)")
    pe_off = struct.unpack_from("<I", pe_data, 0x3C)[0]
    if pe_data[pe_off:pe_off + 4] != b"PE\x00\x00":
        raise ValueError("not a PE (missing PE signature)")
    num_sections = struct.unpack_from("<H", pe_data, pe_off + 6)[0]
    opt_size = struct.unpack_from("<H", pe_data, pe_off + 20)[0]
    opt_off = pe_off + 24
    image_base = struct.unpack_from("<I", pe_data, opt_off + 28)[0]
    sec_off = opt_off + opt_size
    sections = {}
    for i in range(num_sections):
        off = sec_off + i * 40
        name = pe_data[off:off + 8].split(b"\x00", 1)[0].decode("ascii", errors="replace")
        vsize, vaddr = struct.unpack_from("<II", pe_data, off + 8)
        # Keep first occurrence (PE can have duplicate section names)
        if name not in sections:
            sections[name] = {
                "address": image_base + vaddr,
                "size": vsize,
            }
    return sections


def decompress_xex_to_pe_data(xex_path: Path) -> bytes:
    data = xex_path.read_bytes()
    if data[0:4] != b"XEX2":
        raise ValueError("not a XEX2 file")
    pe_offset = struct.unpack(">I", data[8:12])[0]
    opt_count = struct.unpack(">I", data[20:24])[0]

    bff_offset = None
    off = 24
    for _ in range(opt_count):
        hdr_id = struct.unpack(">I", data[off:off + 4])[0]
        hdr_val = struct.unpack(">I", data[off + 4:off + 8])[0]
        if hdr_id == 0x000003FF:
            bff_offset = hdr_val
            break
        off += 8
    if bff_offset is None:
        raise ValueError("no Base File Format header")

    size = struct.unpack(">I", data[bff_offset:bff_offset + 4])[0]
    enc_type = struct.unpack(">H", data[bff_offset + 4:bff_offset + 6])[0]
    comp_type = struct.unpack(">H", data[bff_offset + 6:bff_offset + 8])[0]

    def expand(payload: bytes) -> bytes:
        if comp_type == 0:
            return payload
        if comp_type == 1:
            num_blocks = (size - 8) // 8
            out = bytearray()
            data_offset = 0
            for i in range(num_blocks):
                block_off = bff_offset + 8 + i * 8
                blk_size = struct.unpack(">I", data[block_off:block_off + 4])[0]
                blk_zeros = struct.unpack(">I", data[block_off + 4:block_off + 8])[0]
                out.extend(payload[data_offset:data_offset + blk_size])
                data_offset += blk_size
                out.extend(b"\x00" * blk_zeros)
            return bytes(out)
        raise ValueError(f"unsupported XEX compression type {comp_type}")

    payload = data[pe_offset:]
    if enc_type == 1:
        # AES-128-CBC (IV=0) over the whole payload; the per-file key sits in
        # the security info (header +16 -> offset; file key at +336) and is
        # itself decrypted with the retail or devkit (all-zero) master key.
        # Same scheme as jeff's src/util/xex.rs; try devkit first since the
        # decomp targets the debug build.
        from cryptography.hazmat.primitives.ciphers import (
            Cipher, algorithms, modes,
        )
        sec_off = struct.unpack(">I", data[16:20])[0]
        file_key = data[sec_off + 336:sec_off + 352]
        retail_master = bytes([
            0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3,
            0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91,
        ])
        iv0 = b"\x00" * 16
        trimmed = payload[:len(payload) - (len(payload) % 16)]
        for master in (b"\x00" * 16, retail_master):
            dec = Cipher(algorithms.AES(master), modes.CBC(iv0)).decryptor()
            session_key = dec.update(file_key) + dec.finalize()
            dec = Cipher(algorithms.AES(session_key), modes.CBC(iv0)).decryptor()
            candidate = dec.update(trimmed) + dec.finalize()
            if expand(candidate[:4096])[:2] == b"MZ":
                payload = candidate
                break
        else:
            raise ValueError("encrypted XEX: neither devkit nor retail key fits")
    elif enc_type != 0:
        raise ValueError(f"unsupported XEX encryption type {enc_type}")

    pe_data = expand(payload)
    if pe_data[:2] != b"MZ":
        raise ValueError("decompressed XEX did not contain a PE")
    return pe_data


def should_include_text_symbol(name: str) -> bool:
    return (
        name.startswith("Nui")
        or name.startswith("Nuip")
        or name.startswith("D3DDevice_Nui")
        or name.startswith("CXbcImpl::")
    )


def canonicalize_map_symbol_name(name: str) -> str:
    # Linker maps use MSVC mangled names for many C++ symbols. Map a small set
    # of known DC3/Xenia patch targets back to the semantic names Xenia uses.
    if name.startswith("?") and "@@" in name:
        first_at = name.find("@")
        method = name[1:first_at] if first_at > 1 else ""
        cls_start = first_at + 1
        cls_end = name.find("@@", cls_start)
        cls = name[cls_start:cls_end] if cls_end != -1 else ""
        if cls == "CXbcImpl" and method in {"Initialize", "DoWork", "SendJSON"}:
            return f"CXbcImpl::{method}"
        if method.startswith("D3DDevice_Nui"):
            return method
    return name


def parse_symbols(symbols_path: Path) -> Tuple[Dict[str, dict], Dict[str, dict]]:
    targets: Dict[str, dict] = {}
    crt_sentinels: Dict[str, dict] = {}

    with symbols_path.open("r", encoding="utf-8", errors="replace") as f:
      for line in f:
        line = line.rstrip("\n")
        m = SYMBOL_RE.match(line)
        if not m:
          continue
        name = m.group("name").strip()
        section = m.group("section")
        address = int(m.group("addr"), 16)
        meta = m.group("meta") or ""
        type_match = TYPE_RE.search(meta)
        size_match = SIZE_RE.search(meta)
        entry = {
            "address": address,
            "section": section,
        }
        if type_match:
            entry["type"] = type_match.group(1)
        if size_match:
            entry["size"] = int(size_match.group(1), 16)

        if name in CRT_SENTINELS:
            crt_sentinels[name] = entry
            continue

        if section == ".text" and should_include_text_symbol(name):
            targets[name] = entry

    return targets, crt_sentinels


def parse_pe_section_bases(pe_data: bytes) -> Dict[int, int]:
    """Returns {1-based section index -> VA start} from a PE."""
    pe_off = struct.unpack_from("<I", pe_data, 0x3C)[0]
    image_base = struct.unpack_from("<I", pe_data, pe_off + 24 + 28)[0]
    num_sections = struct.unpack_from("<H", pe_data, pe_off + 6)[0]
    opt_size = struct.unpack_from("<H", pe_data, pe_off + 20)[0]
    sec_off = pe_off + 24 + opt_size
    bases = {}
    for i in range(num_sections):
        off = sec_off + i * 40
        vaddr = struct.unpack_from("<I", pe_data, off + 12)[0]
        bases[i + 1] = image_base + vaddr
    return bases


def parse_map_public_symbols(
    map_path: Path,
    section_remap: Optional[Dict[int, int]] = None,
) -> Tuple[Dict[str, int], Dict[str, str]]:
    """Returns (name->address, name->obj_file) dicts.

    If section_remap is provided, recompute absolute addresses using the
    XEX PE section bases instead of the linker PE addresses.  The remap
    dict maps {1-based section index -> XEX VA base}.
    """
    symbols: Dict[str, int] = {}
    obj_files: Dict[str, str] = {}
    with map_path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = MAP_PUBLIC_RE.match(line.rstrip("\n"))
            if not m:
                continue
            name = canonicalize_map_symbol_name(m.group("name"))
            seg = int(m.group("seg"), 16)
            seg_off = int(m.group("off"), 16)
            abs_addr = int(m.group("abs"), 16)

            if section_remap and seg in section_remap:
                abs_addr = section_remap[seg] + seg_off

            symbols[name] = abs_addr
            obj = m.group("obj")
            if obj:
                obj_files[name] = obj
    return symbols, obj_files


def infer_build_label(pe_path: Path) -> str:
    s = str(pe_path).lower()
    if "/orig/" in s or "\\orig\\" in s:
        return "original"
    if "/build/" in s or "\\build\\" in s:
        return "decomp"
    return "unknown"


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate Xenia DC3 patch manifest")
    parser.add_argument("--pe", default=str(DEFAULT_PE), help="Path to linked PE (default.exe)")
    parser.add_argument("--xex", default=None, help="Optional built XEX path for runtime-accurate .text hash")
    parser.add_argument("--symbols", default=str(DEFAULT_SYMBOLS), help="Path to symbols.txt")
    parser.add_argument("--map", dest="map_path", default=str(DEFAULT_MAP),
                        help="Optional linker .map file for decomp addresses (default: build/373307D9/default.map)")
    parser.add_argument("--pe-map", dest="pe_map_path", default=str(DEFAULT_PE_MAP),
                        help="Optional PE MAP file with internal symbols (default: build/373307D9/default.exe.MAP)")
    parser.add_argument("--output", "-o", default=str(DEFAULT_OUT), help="Output manifest JSON")
    parser.add_argument(
        "--xenia-runtime-fnv1a64",
        default=None,
        help="Optional Xenia runtime .text FNV1a64 (hex) to embed as "
             "pe.text.xenia_runtime_fnv1a64 for exact layout matching",
    )
    parser.add_argument("--title-id", default="373307D9", help="Title ID (hex)")
    parser.add_argument(
        "--build-label",
        choices=["original", "decomp", "unknown"],
        default=None,
        help="Override build label (default: infer from PE path)",
    )
    args = parser.parse_args()

    pe_path = Path(args.pe)
    xex_path = Path(args.xex) if args.xex else None
    symbols_path = Path(args.symbols)
    map_path = Path(args.map_path) if args.map_path else None
    pe_map_path = Path(args.pe_map_path) if args.pe_map_path else None
    out_path = Path(args.output)

    if not pe_path.exists():
        print(f"error: PE not found: {pe_path}")
        return 1
    if not symbols_path.exists():
        print(f"error: symbols.txt not found: {symbols_path}")
        return 1

    pe_data = pe_path.read_bytes()
    static_text_info = parse_pe_text_info(pe_data)
    text_info = dict(static_text_info)
    xex_pe_data: Optional[bytes] = None
    section_remap: Optional[Dict[int, int]] = None
    if xex_path is not None:
        if not xex_path.exists():
            print(f"error: XEX not found: {xex_path}")
            return 1
        xex_pe_data = decompress_xex_to_pe_data(xex_path)
        runtime_text_info = parse_pe_text_info(xex_pe_data)
        # Prefer the runtime-equivalent fingerprint/range if provided.
        text_info["address"] = runtime_text_info["address"]
        text_info["rva"] = runtime_text_info["rva"]
        text_info["size"] = runtime_text_info["size"]
        text_info["raw_size"] = runtime_text_info["raw_size"]
        text_info["fnv1a64"] = runtime_text_info["fnv1a64"]
        # Compute section remap: MAP segment indices -> XEX PE section VAs.
        # The XEX builder may change section virtual addresses vs the linker PE.
        pe_bases = parse_pe_section_bases(pe_data)
        xex_bases = parse_pe_section_bases(xex_pe_data)
        if pe_bases != xex_bases:
            section_remap = xex_bases
            deltas = {i: xex_bases.get(i, 0) - pe_bases.get(i, 0)
                      for i in pe_bases if i in xex_bases}
            unique_deltas = set(deltas.values())
            print(f"  section remap: {len(deltas)} sections, "
                  f"deltas: {', '.join(f'+0x{d:X}' if d >= 0 else f'-0x{-d:X}' for d in sorted(unique_deltas))}")
    targets, crt_sentinels = parse_symbols(symbols_path)

    # Apply section remap to symbols.txt addresses (targets & crt_sentinels).
    # symbols.txt has PE addresses; when the XEX builder changes section bases,
    # these must be translated to XEX addresses — same as MAP symbols.
    if xex_pe_data is not None:
        pe_sections = parse_pe_section_info(pe_data)
        xex_sections = parse_pe_section_info(xex_pe_data)
        # Build {section_name: delta} mapping.
        section_name_deltas: Dict[str, int] = {}
        for sec_name in pe_sections:
            if sec_name in xex_sections:
                d = xex_sections[sec_name]["address"] - pe_sections[sec_name]["address"]
                if d != 0:
                    section_name_deltas[sec_name] = d
        if section_name_deltas:
            remapped = 0
            for entries in (targets, crt_sentinels):
                for entry in entries.values():
                    sec = entry.get("section", "")
                    if sec in section_name_deltas:
                        entry["address"] += section_name_deltas[sec]
                        remapped += 1
            print(f"  symbols.txt remap: applied delta to {remapped} entries "
                  f"({', '.join(f'{k}:{d:+#X}' for k, d in section_name_deltas.items())})")
    map_symbols: Dict[str, int] = {}
    map_obj_files: Dict[str, str] = {}
    if map_path and map_path.exists():
        map_symbols, map_obj_files = parse_map_public_symbols(
            map_path, section_remap=section_remap)
        for name, address in map_symbols.items():
            if name in CRT_SENTINELS:
                crt_sentinels.setdefault(name, {})["address"] = address
                crt_sentinels[name].setdefault("section", ".data")
                continue
            if should_include_text_symbol(name):
                targets.setdefault(name, {})["address"] = address
                targets[name].setdefault("section", ".text")
        # Prefer .map addresses when present for existing entries.
        for name, entry in list(targets.items()):
            if name in map_symbols:
                entry["address"] = map_symbols[name]
        for name, entry in list(crt_sentinels.items()):
            if name in map_symbols:
                entry["address"] = map_symbols[name]

    # Resolve hack-pack stub targets from MAP, falling back to symbols.txt
    hack_pack_stubs: Dict[str, dict] = {}
    hack_pack_found = 0
    hack_pack_missing = 0
    hack_pack_from_symbols = 0
    for display_name, mangled_name in HACK_PACK_STUBS.items():
        if map_symbols and mangled_name in map_symbols:
            hack_pack_stubs[display_name] = {
                "address": map_symbols[mangled_name],
                "section": ".text",
                "map_symbol": mangled_name,
            }
            hack_pack_found += 1
        elif mangled_name in targets:
            hack_pack_stubs[display_name] = {
                "address": targets[mangled_name]["address"],
                "section": targets[mangled_name].get("section", ".text"),
                "map_symbol": mangled_name,
            }
            hack_pack_found += 1
            hack_pack_from_symbols += 1
        else:
            hack_pack_missing += 1
    if hack_pack_from_symbols > 0:
        print(f"  hack_pack_stubs: {hack_pack_found} found "
              f"({hack_pack_from_symbols} from symbols.txt fallback), "
              f"{hack_pack_missing} missing")
    if hack_pack_missing > 0:
        missing = [dn for dn, mn in HACK_PACK_STUBS.items()
                   if mn not in map_symbols and mn not in targets]
        if missing:
            print(f"  hack_pack_stubs missing: {missing[:5]}{'...' if len(missing) > 5 else ''}")

    # Auto-collect XDK SDK functions for blanket JIT override stubbing.
    # TODO: Remove these blanket overrides once Xenia's APU/NUI backends
    # properly handle these subsystems or the decomp provides its own.
    #
    # XAUDIO2: nop APU has no render thread -> CX2 methods deadlock on CS.
    # NUI: headless mode has no Kinect -> NUI functions loop/crash.
    XDK_OVERRIDE_OBJ_PATTERNS = {
        # XAUDIO2 internal implementation objects
        "x2voice", "x2engine",
        # NUI (Kinect SDK) objects
        "nui",
        # Speech recognition
        "speech",
    }
    xdk_overrides: Dict[str, dict] = {}
    text_start = text_info["address"]
    text_end = text_start + text_info["size"]
    if map_symbols and map_obj_files:
        for name, address in map_symbols.items():
            # Skip vtable entries and string constants (data, not code)
            if "@@6B" in name or name.startswith("??_C@"):
                continue
            # Only include symbols in the .text section
            if not (text_start <= address < text_end):
                continue
            obj = map_obj_files.get(name, "")
            obj_lower = obj.lower()
            if any(obj_lower.startswith(pat) for pat in XDK_OVERRIDE_OBJ_PATTERNS):
                xdk_overrides[name] = {
                    "address": address,
                    "section": ".text",
                    "obj": obj,
                }
    # Also parse PE MAP (default.exe.MAP) for internal/static symbols not in
    # the linker MAP. The PE MAP includes symbols from COFF objects that are
    # internal (non-public) linkage, catching internal XDK functions that the
    # linker MAP omits.
    if pe_map_path and pe_map_path.exists():
        pe_map_syms, pe_map_objs = parse_map_public_symbols(
            pe_map_path, section_remap=section_remap)
        pe_map_added = 0
        for name, address in pe_map_syms.items():
            if name in xdk_overrides:
                continue  # already have it from linker MAP
            if "@@6B" in name or name.startswith("??_C@"):
                continue
            if not (text_start <= address < text_end):
                continue
            obj = pe_map_objs.get(name, "")
            obj_lower = obj.lower()
            if any(obj_lower.startswith(pat) for pat in XDK_OVERRIDE_OBJ_PATTERNS):
                xdk_overrides[name] = {
                    "address": address,
                    "section": ".text",
                    "obj": obj,
                }
                pe_map_added += 1
        if pe_map_added > 0:
            print(f"  xdk_overrides: +{pe_map_added} internal symbols from PE MAP")
    if xdk_overrides:
        print(f"  xdk_overrides: {len(xdk_overrides)} total methods collected")

    # Compute XDK code ranges: contiguous address blocks from XDK obj files.
    # Used by Xenia to scan for unlisted internal function prologues.
    xdk_code_ranges: list = []
    if xdk_overrides:
        # Get all XDK function addresses sorted
        xdk_addrs = sorted(
            d["address"] for d in xdk_overrides.values()
            if isinstance(d, dict) and d.get("address", 0) >= 0x822C0000
        )
        if xdk_addrs:
            # Also build a sorted list of ALL symbol addresses from both MAPs
            # so we can find the boundary after the last XDK symbol in a block.
            all_addrs = sorted(set(map_symbols.values()))
            if pe_map_path and pe_map_path.exists():
                all_addrs = sorted(set(all_addrs) | set(pe_map_syms.values()))
            all_addrs = [a for a in all_addrs if a >= 0x822C0000]

            # For each XDK symbol, find the next symbol address (XDK or not)
            # to determine the extent of the XDK function.
            xdk_addr_set = set(xdk_addrs)
            ranges = []
            for xdk_addr in xdk_addrs:
                # Find next symbol after this one
                import bisect
                idx = bisect.bisect_right(all_addrs, xdk_addr)
                next_addr = all_addrs[idx] if idx < len(all_addrs) else xdk_addr + 0x100
                ranges.append((xdk_addr, next_addr))

            # Merge overlapping/adjacent ranges (within 16 bytes gap)
            merged = []
            for start, end in sorted(ranges):
                if merged and start <= merged[-1][1] + 16:
                    merged[-1] = (merged[-1][0], max(merged[-1][1], end))
                else:
                    merged.append((start, end))

            xdk_code_ranges = [{"start": s, "end": e} for s, e in merged]
            total_bytes = sum(e - s for s, e in merged)
            print(f"  xdk_code_ranges: {len(xdk_code_ranges)} blocks, "
                  f"{total_bytes} bytes total")

    # Resolve address catalog from MAP + PE for Dc3Addresses runtime population.
    # Falls back to symbols.txt when MAP file is empty/missing.
    address_catalog: Dict[str, dict] = {}
    catalog_found = 0
    catalog_from_symbols = 0
    catalog_missing = []
    for field_name, map_sym in ADDRESS_CATALOG.items():
        if map_symbols and map_sym in map_symbols:
            address_catalog[field_name] = {
                "address": map_symbols[map_sym],
            }
            catalog_found += 1
        elif map_sym in targets:
            address_catalog[field_name] = {
                "address": targets[map_sym]["address"],
            }
            catalog_found += 1
            catalog_from_symbols += 1
        else:
            catalog_missing.append(field_name)
    if catalog_from_symbols > 0:
        print(f"  address_catalog: {catalog_found} found "
              f"({catalog_from_symbols} from symbols.txt fallback), "
              f"{len(catalog_missing)} missing")
    if catalog_missing:
        print(f"  address_catalog missing: {catalog_missing[:10]}"
              f"{'...' if len(catalog_missing) > 10 else ''}")

    # PE-derived fields: .text and .idata section info
    pe_sections = parse_pe_section_info(xex_pe_data if xex_pe_data else pe_data)
    address_catalog["text_start"] = {"address": text_info["address"]}
    address_catalog["text_size"] = {"address": text_info["size"]}
    if ".idata" in pe_sections:
        idata = pe_sections[".idata"]
        address_catalog["idata_start"] = {"address": idata["address"]}
        address_catalog["idata_end"] = {"address": idata["address"] + idata["size"]}

    # Thunk area: in the decomp PE, import thunks are scattered across .text
    # (build_xex.py converts them to XEX markers in-place), so the thunk area
    # spans the entire .text section.
    address_catalog["thunk_area_start"] = {"address": text_info["address"]}
    address_catalog["thunk_area_end"] = {
        "address": text_info["address"] + text_info["size"],
    }

    # CRT$XCU section: decomp's static initializer function pointers.
    # The linker merges .CRT$XC* subsections into a single .CRT section.
    # This section exists only in the DECOMP PE (the original links its CRT
    # tables into .data), so it must be read from pe_data -- pe_sections above
    # prefers the original XEX's PE and silently never contains ".CRT", which
    # left crt_xcu_* out of every manifest and xenia running on stale compiled
    # defaults (83627800..83627B44).  Found 2026-08-23 when the first relink
    # since Aug 4 moved .CRT and boot injected 209 code words as constructors.
    decomp_sections = parse_pe_section_info(pe_data)
    if ".CRT" in decomp_sections:
        crt = decomp_sections[".CRT"]
        address_catalog["crt_xcu_start"] = {"address": crt["address"]}
        address_catalog["crt_xcu_end"] = {"address": crt["address"] + crt["size"]}

    # Computed: g_hash_table = gStringTable + 4 (/FORCE linker artifact)
    if "g_string_table_global" in address_catalog:
        address_catalog["g_hash_table"] = {
            "address": address_catalog["g_string_table_global"]["address"] + 4,
        }

    if address_catalog:
        print(f"  address_catalog: {len(address_catalog)} entries total")

    build_label = args.build_label or infer_build_label(pe_path)

    xenia_runtime_fingerprint: Optional[int] = None
    if args.xenia_runtime_fnv1a64:
        parsed_runtime_fp = 0
        value = args.xenia_runtime_fnv1a64.strip()
        if value.lower().startswith("0x"):
            value = value[2:]
        try:
            parsed_runtime_fp = int(value, 16)
        except ValueError:
            print(f"error: invalid --xenia-runtime-fnv1a64: {args.xenia_runtime_fnv1a64}")
            return 1
        xenia_runtime_fingerprint = parsed_runtime_fp & 0xFFFFFFFFFFFFFFFF

    manifest = {
        "schema_version": 1,
        "format_version": 1,
        "schema": "xenia.dc3.nui_patch_manifest",
        "title_id": args.title_id.upper(),
        "build_label": build_label,
        "build_identity": {
            "title_id": args.title_id.upper(),
            "build_label": build_label,
        },
        "pe": {
            "image_base": text_info["image_base"],
            "text": {
                "rva": text_info["rva"],
                "address": text_info["address"],
                "size": text_info["size"],
                "raw_size": text_info["raw_size"],
                "fnv1a64": text_info["fnv1a64"],
                "fnv1a64_static_pe": static_text_info["fnv1a64"],
                "xenia_runtime_fnv1a64": xenia_runtime_fingerprint,
            },
        },
        "targets": targets,
        "crt_sentinels": crt_sentinels,
        "hack_pack_stubs": hack_pack_stubs,
        "xdk_overrides": xdk_overrides,
        "xdk_code_ranges": xdk_code_ranges,
        "address_catalog": address_catalog,
        "sources": {
            "pe": str(pe_path),
            "xex": str(xex_path) if xex_path else None,
            "symbols": str(symbols_path),
            "map": str(map_path) if map_path and map_path.exists() else None,
        },
    }

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8")

    print(f"Wrote manifest: {out_path}")
    print(f"  build_label={build_label}")
    print(f"  .text addr=0x{text_info['address']:08X} size=0x{text_info['size']:X} "
          f"fnv1a64=0x{text_info['fnv1a64']:016X}")
    print(f"  .text static_pe_fnv1a64=0x{static_text_info['fnv1a64']:016X}")
    if xenia_runtime_fingerprint is not None:
        print(f"  .text xenia_runtime_fnv1a64=0x{xenia_runtime_fingerprint:016X}")
    print(f"  targets={len(targets)} crt_sentinels={len(crt_sentinels)} "
          f"hack_pack_stubs={len(hack_pack_stubs)} "
          f"xdk_overrides={len(xdk_overrides)} "
          f"address_catalog={len(address_catalog)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

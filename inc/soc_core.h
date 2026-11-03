/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

// Asset loading/data API. Deliberately excludes document, view and World.
#ifndef SOC_CORE_H
#define SOC_CORE_H
#include "kauai_core.h"
#include "bren_core.h"
#include "socdef.h"
#include "tagman.h"
#include "tagl.h"
#include "modl.h"
#include "mtrl.h"
#include "body.h"
#include "tmpl.h"
#include "tdf.h"
#include "tdt.h"
#include "movie_data.h"
#include "scene_data.h"
#include "background_data.h"
#include "actor_data.h"
#include "actor_track.h"
#include "actor_route.h"
#include "actor_state.h"
#include "actor_pose.h"
#include "actor_frame.h"
#include "actor_timeline.h"
#include "actor_seek.h"
#include "text_box_data.h"
#include "sound_queue.h"
#include "movie_playback.h"

#define kctgActn 'ACTN'
#define kctgActr 'ACTR'
#define kctgBds 'BDS '
#define kctgBkgd 'BKGD'
#define kctgBmdl 'BMDL'
#define kctgBpmp 'BPMP'
#define kctgCam 'CAM '
#define kctgGgae 'GGAE'
#define kctgGldc 'GLDC' // REVIEW *****: obsolete
#define kctgGgcm 'GGCM'
#define kctgGllt 'GLLT'
#define kctgGlms 'GLMS' // motion-match sounds (under ActionDefinition)
#define kctgGgcl 'GGCL'
#define kctgGlxf 'GLXF'
#define kctgMsnd 'MSND'
#define kctgMtrl 'MTRL'
#define kctgCmtl 'CMTL'
#define kctgMvie 'MVIE'
#define kctgPath 'PATH'
#define kctgPict 'PICT'
#define kctgScen 'SCEN'
#define kctgSnd 'SND '
#define kctgSoc 'SOC '
#define kctgTbox 'TBOX'
#define kctgTdf 'TDF '
#define kctgTdt 'TDT '
#define kctgTmpl 'TMPL'
#define kctgGlpi 'GLPI'
#define kctgGlbs 'GLBS'
#define kctgInfo 'INFO'
#define kctgFrmGg 'GGFR'
#define kctgStartGg 'GGST'
#define kctgThumbMbmp 'THUM'
#define kctgGltm 'GLTM'
#define kctgGlbk 'GLBK'
#define kctgGlcg 'GLCG'
#define kctgBkth 'BKTH' // Background thumbnail
#define kctgCath 'CATH' // Camera thumbnail
#define kctgTmth 'TMTH' // Template thumbnail (non-prop)
#define kctgPrth 'PRTH' // Prop thumbnail
#define kctgAnth 'ANTH' // Action thumbnail
#define kctgSvth 'SVTH' // Sounds (voice) thumbnail
#define kctgSfth 'SFTH' // Sounds (FX) thumbnail
#define kctgSmth 'SMTH' // Sounds (midi) thumbnail
#define kctgMtth 'MTTH' // Materials thumbnail
#define kctgCmth 'CMTH' // Custom materials thumbnail
#define kctgTsth 'TSTH' // 3d shape thumbnail
#define kctgTfth 'TFTH' // 3d font thumbnail
#define kctgTcth 'TCTH' // Text color thumbnail
#define kctgTbth 'TBTH' // Text background thumbnail
#define kctgTzth 'TZTH' // Text size thumbnail
#define kctgTyth 'TYTH' // Text style thumbnail

#define kftgChunky MacWin('chnk', 'CHK')
#define kftgContent MacWin('3con', '3CN')
#define kftgThumbDesc MacWin('3thd', '3TH')
#define kftg3mm MacWin('3mm', '3MM')
#define kftgSocTemp MacWin('3tmp', '3TP')

#define ksz3mm PszLit("3mm")

#endif

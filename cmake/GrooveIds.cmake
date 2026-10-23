# ---------------------------------------------------------------------------
# Groove Knobs — plugin identifiers.
# Single source of truth: every name / code / bundle id used by the build,
# the plugin code (via compile definitions) and the validation scripts
# (scripts/run_auval.sh greps this file) lives here.
# ---------------------------------------------------------------------------

# Suite-wide
set(GK_SUITE_NAME          "Groove Knobs")
set(GK_SUITE_VERSION       "0.1.6")          # the suite installer (= every plug-in's version for a release)
set(GK_COMPANY_NAME        "LALZIN")
set(GK_MANUFACTURER_CODE   "Lalz")
set(GK_BUNDLE_ID_PREFIX    "com.lalzin.grooveknobs")

# French Filter
set(FF_PRODUCT_NAME        "French Filter")
set(FF_PLUGIN_CODE         "FrFl")
set(FF_AU_TYPE             "aufx")          # kAudioUnitType_Effect
set(FF_BUNDLE_ID           "${GK_BUNDLE_ID_PREFIX}.frenchfilter")
set(FF_VERSION             "0.1.6")

# Boogie Trio
set(BT_PRODUCT_NAME        "Boogie Trio")
set(BT_PLUGIN_CODE         "BgTr")
set(BT_AU_TYPE             "aufx")          # kAudioUnitType_Effect
set(BT_BUNDLE_ID           "${GK_BUNDLE_ID_PREFIX}.boogietrio")
set(BT_VERSION             "0.1.6")

# Backwash
set(BW_PRODUCT_NAME        "Backwash")
set(BW_PLUGIN_CODE         "Bkws")
set(BW_AU_TYPE             "aufx")          # kAudioUnitType_Effect
set(BW_BUNDLE_ID           "${GK_BUNDLE_ID_PREFIX}.backwash")
set(BW_VERSION             "0.1.6")

# Pew Pew (instrument)
set(PP_PRODUCT_NAME        "Pew Pew")
set(PP_PLUGIN_CODE         "PwPw")
set(PP_AU_TYPE             "aumu")          # kAudioUnitType_MusicDevice
set(PP_BUNDLE_ID           "${GK_BUNDLE_ID_PREFIX}.pewpew")
set(PP_VERSION             "0.1.6")

# Sing It Back (vocal effect)
set(SIB_PRODUCT_NAME       "Sing It Back")
set(SIB_PLUGIN_CODE        "SItB")
set(SIB_AU_TYPE            "aufx")          # kAudioUnitType_Effect
set(SIB_BUNDLE_ID          "${GK_BUNDLE_ID_PREFIX}.singitback")
set(SIB_VERSION            "0.1.6")

# Kick Lock (sidechain ducking + sub phase lock) — working name
set(KL_PRODUCT_NAME        "Kick Lock")
set(KL_PLUGIN_CODE         "KkLk")
set(KL_AU_TYPE             "aufx")          # kAudioUnitType_Effect
set(KL_BUNDLE_ID           "${GK_BUNDLE_ID_PREFIX}.kicklock")
set(KL_VERSION             "0.1.6")

set(KLS_PRODUCT_NAME       "Kick Lock Send")
set(KLS_PLUGIN_CODE        "KkLs")
set(KLS_AU_TYPE            "aufx")          # kAudioUnitType_Effect
set(KLS_BUNDLE_ID          "${GK_BUNDLE_ID_PREFIX}.kicklocksend")
set(KLS_VERSION            "${KL_VERSION}")  # ships with Kick Lock

# Slapper (physical-modelling funk bass instrument, + synth engines)
set(SL_PRODUCT_NAME        "Slapper")
set(SL_PLUGIN_CODE         "Slpr")
set(SL_AU_TYPE             "aumu")          # kAudioUnitType_MusicDevice
set(SL_BUNDLE_ID           "${GK_BUNDLE_ID_PREFIX}.slapper")
set(SL_VERSION             "0.1.6")

# Crackle (one-knob lo-fi grain: vinyl, tape, radio, bitcrush) — in test, not in the suite yet
set(CR_PRODUCT_NAME        "Crackle")
set(CR_PLUGIN_CODE         "Crkl")
set(CR_AU_TYPE             "aufx")          # kAudioUnitType_Effect
set(CR_BUNDLE_ID           "${GK_BUNDLE_ID_PREFIX}.crackle")
set(CR_VERSION             "0.1.0")

# ---------------------------------------------------------------------------
# Groove Knobs — plugin identifiers.
# Single source of truth: every name / code / bundle id used by the build,
# the plugin code (via compile definitions) and the validation scripts
# (scripts/run_auval.sh greps this file) lives here.
# ---------------------------------------------------------------------------

# Suite-wide
set(GK_SUITE_NAME          "Groove Knobs")
set(GK_COMPANY_NAME        "LALZIN")
set(GK_MANUFACTURER_CODE   "Lalz")
set(GK_BUNDLE_ID_PREFIX    "com.lalzin.grooveknobs")

# French Filter
set(FF_PRODUCT_NAME        "French Filter")
set(FF_PLUGIN_CODE         "FrFl")
set(FF_AU_TYPE             "aufx")          # kAudioUnitType_Effect
set(FF_BUNDLE_ID           "${GK_BUNDLE_ID_PREFIX}.frenchfilter")
set(FF_VERSION             "0.1.2")

# Boogie Trio
set(BT_PRODUCT_NAME        "Boogie Trio")
set(BT_PLUGIN_CODE         "BgTr")
set(BT_AU_TYPE             "aufx")          # kAudioUnitType_Effect
set(BT_BUNDLE_ID           "${GK_BUNDLE_ID_PREFIX}.boogietrio")
set(BT_VERSION             "0.1.2")

# Backwash
set(BW_PRODUCT_NAME        "Backwash")
set(BW_PLUGIN_CODE         "Bkws")
set(BW_AU_TYPE             "aufx")          # kAudioUnitType_Effect
set(BW_BUNDLE_ID           "${GK_BUNDLE_ID_PREFIX}.backwash")
set(BW_VERSION             "0.1.2")

# Pew Pew (instrument)
set(PP_PRODUCT_NAME        "Pew Pew")
set(PP_PLUGIN_CODE         "PwPw")
set(PP_AU_TYPE             "aumu")          # kAudioUnitType_MusicDevice
set(PP_BUNDLE_ID           "${GK_BUNDLE_ID_PREFIX}.pewpew")
set(PP_VERSION             "0.1.2")

# Sing It Back (vocal effect)
set(SIB_PRODUCT_NAME       "Sing It Back")
set(SIB_PLUGIN_CODE        "SItB")
set(SIB_AU_TYPE            "aufx")          # kAudioUnitType_Effect
set(SIB_BUNDLE_ID          "${GK_BUNDLE_ID_PREFIX}.singitback")
set(SIB_VERSION            "0.1.2")

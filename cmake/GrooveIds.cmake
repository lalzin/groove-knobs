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
set(FF_VERSION             "0.1.0")

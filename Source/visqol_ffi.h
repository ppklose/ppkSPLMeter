#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define VISQOL_MAX_BANDS 32

/**
 * Result of a ViSQOL analysis run.
 * On failure, error_msg[0] != '\0' and the numeric fields are zero.
 */
typedef struct {
    double moslqo;                          /**< MOS-LQO score (1=bad, 5=excellent) */
    double vnsim;                           /**< Mean NSIM across all bands           */
    double fvnsim[VISQOL_MAX_BANDS];        /**< Per-band NSIM scores                 */
    double center_freq_bands[VISQOL_MAX_BANDS]; /**< Centre frequency of each band (Hz) */
    int    band_count;                      /**< Number of valid entries in the arrays */
    char   error_msg[256];                  /**< Non-empty string on failure           */
} VisqolResult;

/**
 * Run ViSQOL on two WAV files.
 *
 * @param ref_path  Absolute path to the reference WAV file (null-terminated).
 * @param deg_path  Absolute path to the degraded WAV file (null-terminated).
 * @param mode      0 = Wideband (speech, 21 bands),
 *                  1 = Fullband (audio, 32 bands — needs model file).
 * @param out       Caller-allocated result struct; filled on return.
 * @return          0 on success, -1 on error (check out->error_msg).
 */
int visqol_run(const char* ref_path,
               const char* deg_path,
               int         mode,
               VisqolResult* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

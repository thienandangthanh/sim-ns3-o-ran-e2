/*
 * kpm-func-desc-v3-test.cc — Phase 4 (area C) standalone correctness proof for
 * the E2SM-KPM v3.00 RAN-function-description.
 *
 * Builds the descriptor via kpm-func-desc-v3.h (the ns-3-side encoder logic,
 * minus ns-3), then:
 *   1. APER-encodes it (must succeed, length > 0),
 *   2. decodes the bytes back into a fresh struct (round-trip must succeed and
 *      consume the whole buffer) — proves the bytes are valid E2SM-KPM v3.00,
 *   3. XER-prints the decoded struct to stderr for human inspection,
 *   4. writes the raw APER bytes to argv[1] (default: kpm-func-desc-v3.bin)
 *      so e2-setup-minimal / pcap diffs can consume the exact descriptor.
 *
 * Exit code 0 = M3-precondition green (valid v3.00 descriptor encoded).
 *
 * Build: linked against installed libe2sim.a + asn1_objects OBJECT lib
 *        (same recipe as e2-setup-minimal, see CMakeLists in the Phase-4 notes).
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "kpm-func-desc-v3.h"

extern "C" {
#include "E2SM-KPM-RANfunction-Description.h"
#include "aper_decoder.h"
}

int
main (int argc, char *argv[])
{
  const char *out_path = (argc > 1) ? argv[1] : "kpm-func-desc-v3.bin";

  /* 1. Build + encode. */
  E2SM_KPM_RANfunction_Description_t *desc =
      (E2SM_KPM_RANfunction_Description_t *) calloc (1, sizeof (*desc));
  kpm_v3::FillKpmFunctionDescription (desc);

  uint8_t *buf = 0;
  long len = kpm_v3::EncodeKpmFunctionDescription (desc, &buf);
  if (len <= 0)
    {
      fprintf (stderr, "[FAIL] APER encode returned %ld\n", len);
      return 1;
    }
  fprintf (stderr, "[ok]   encoded E2SM-KPM v3.00 descriptor: %ld bytes\n", len);

  /* First bytes (handy for golden byte-diff). */
  fprintf (stderr, "[ok]   first 16 bytes:");
  for (long i = 0; i < len && i < 16; i++)
    fprintf (stderr, " %02x", buf[i]);
  fprintf (stderr, "\n");

  /* 2. Round-trip decode into a fresh struct. */
  E2SM_KPM_RANfunction_Description_t *decoded = 0;
  asn_dec_rval_t rv = aper_decode_complete (
      0, &asn_DEF_E2SM_KPM_RANfunction_Description, (void **) &decoded, buf, len);
  if (rv.code != RC_OK)
    {
      fprintf (stderr, "[FAIL] APER decode rc=%d consumed=%zu/%ld\n", rv.code, rv.consumed, len);
      return 2;
    }
  if ((long) rv.consumed != len)
    {
      fprintf (stderr, "[FAIL] decode consumed %zu of %ld bytes (trailing garbage)\n",
               rv.consumed, len);
      return 3;
    }
  fprintf (stderr, "[ok]   round-trip decode consumed all %ld bytes\n", len);

  /* Sanity on decoded content: 1 trigger style + 5 report styles. */
  int n_trig = decoded->ric_EventTriggerStyle_List
                   ? decoded->ric_EventTriggerStyle_List->list.count
                   : 0;
  int n_rep = decoded->ric_ReportStyle_List ? decoded->ric_ReportStyle_List->list.count : 0;
  fprintf (stderr, "[ok]   decoded: %d event-trigger style(s), %d report style(s)\n", n_trig,
           n_rep);
  if (n_trig != 1 || n_rep != 5)
    {
      fprintf (stderr, "[FAIL] unexpected style counts (want 1 trigger / 5 report)\n");
      return 4;
    }

  /* 3. XER dump for human inspection. */
  fprintf (stderr, "----- XER -----\n");
  xer_fprint (stderr, &asn_DEF_E2SM_KPM_RANfunction_Description, decoded);

  /* 4. Persist raw APER bytes. */
  FILE *f = fopen (out_path, "wb");
  if (!f || fwrite (buf, 1, len, f) != (size_t) len)
    {
      fprintf (stderr, "[FAIL] could not write %s\n", out_path);
      return 5;
    }
  fclose (f);
  fprintf (stderr, "[ok]   wrote %ld descriptor bytes to %s\n", len, out_path);

  /* Cleanup. */
  ASN_STRUCT_FREE (asn_DEF_E2SM_KPM_RANfunction_Description, decoded);
  ASN_STRUCT_FREE_CONTENTS_ONLY (asn_DEF_E2SM_KPM_RANfunction_Description, desc);
  free (desc);
  free (buf);

  fprintf (stderr, "[PASS] E2SM-KPM v3.00 RAN-function-description is valid (M3 precondition)\n");
  return 0;
}

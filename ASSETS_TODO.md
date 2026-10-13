# ASSETS_TODO — Boogie Trio (Velvet)

Velvet works today with the sheet `plugins/boogie_trio/Assets/source/velvet_sheet.png`
(11 poses, cut by `scripts/make_velvet_frames.sh`). Nothing is blocking. The images
below would make him better; drop them in `Assets/source/` and update the frame list
in `scripts/make_velvet_frames.sh` / the roles in `VelvetMascot.h`.

Common to all images: same style as the existing sheet (ball body in violet / orange /
pink bands, black quiff, orange aviator glasses, white gloves, rubber-hose limbs,
striped platform shoes, brown violin), **flat background of one colour** (any colour
that does not appear on the character; the sage green of the current sheet is fine)
or transparent PNG, character centred, feet on the same baseline, at least 30 px of
empty space between characters on a sheet.

| Priority | File | Content | Size / format |
|---|---|---|---|
| 1 | `velvet_sheet_hd.png` | **The same 12 poses in higher resolution.** The current sheet gives ~330 px per character: slightly soft on a Retina screen with the window at 200 % (needs ~700 px). | 4 × 3 grid, ≥ 2400 × 1800 px, PNG |
| 2 | `velvet_vibrato_1.png`, `velvet_vibrato_2.png` | Playing, left hand **on the neck shaking** (two positions of the fingers / hand, motion lines). Today the vibrato is suggested by a small tremble of the whole character. | 1 pose each, same framing as the "playing" poses, PNG |
| 3 | `velvet_euphoric.png` | Playing with a very big smile, sparkles, slightly jumping (for 100 %). Today: the playing poses + vector sparkles. | 1 pose, PNG |
| 4 | `velvet_blink.png` | Waiting pose (violin at rest) with the eyes half open, for the random blinks at 0 %. Today: alternates between "eyes closed" and "glance up". | 1 pose, PNG |

Not used on purpose: the open-mouth singing pose of the current sheet (Alan, 2026-10-13).


# ASSETS_TODO — Backwash (Swell)

Swell works with the sheet `plugins/backwash/Assets/source/swell_sheet.png` (12 poses,
`scripts/make_swell_frames.sh`). The spec planned a second 4 × 4 sheet of loops; today the
breath is animated in code on the existing poses. These frames would make it better
(same style, flat single-colour background or transparent PNG, feet / board on the same
baseline, ≥ 30 px between characters):

| Priority | File | Content | Size |
|---|---|---|---|
| 1 | `swell_breath_sheet.png`, row 1 | **Inhale** cycle, 4 poses: neutral → cheeks puffed to the maximum, eyes wide (picked by the real breath, not by time) | 4 × 4 grid, ≥ 2400 × 1800 px |
| 1 | same sheet, row 2 | **Exhale** and bounce on the hit, 4 poses (cheeks deflating, water splashing) | — |
| 2 | same sheet, row 3 | Rocking on the board while surfing, 4 poses (for the beat) | — |
| 2 | same sheet, row 4 | **Asleep lying on the board**, floating, 4 poses (today: the bored beach pose + "Zzz") | — |
| 3 | `swell_sheet_hd.png` | The current 12 poses in higher resolution (~330 px per character today, slightly soft at 200 % on Retina) | ≥ 2400 × 1800 px |

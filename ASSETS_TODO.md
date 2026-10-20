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


# ASSETS_TODO — Pew Pew (Tomi)

Tomi works today with Alan's two sheets (`plugins/pew_pew/Assets/source/tomi_sheet_1.png`,
`tomi_sheet_2.png`, 19 poses cut by `scripts/make_tomi_frames.sh`). Nothing is blocking.
These would make him better (same style, flat green background, 4 × 4 grid of 256 px
cells **without grid lines**, common foot line, ≥ 2048 × 2048 for a sharp 200 % window):

| Priority | File | Poses | Why |
|---|---|---|---|
| 1 | `tomi_hits.png`, row 1 | Strike cycle seen from the side, **the drum under the sticks**: stick raised → hitting the skin → rebound → back, 4 poses | Today the hit is a pose change in front of a drawn drum; sticks actually on the skin would read much better |
| 1 | same sheet, row 2 | The same at full force (big smile, sticks higher), 4 poses | For 50–100 % |
| 2 | same sheet, row 3 | Swaying and tapping the tempo, 4 poses | Today: two poses of sheet 2 alternate |
| 2 | same sheet, row 4 | Asleep by the drum, **head on a stick**, 2 poses + 2 blinking (eyes half / closed) for the idle | The spec's sleep pose; today he sleeps sitting, sticks on his knees |
| 3 | `tomi_sheet_hd.png` | Sheet 1 in higher resolution, without grid lines | ~250 px per character today |

# ASSETS_TODO — Sing It Back (Sugar)

Sugar works today with Alan's two sheets (`plugins/sing_it_back/Assets/source/sugar_sheet_1.png`,
`sugar_sheet_2.png`, 33 poses cut by `scripts/make_sugar_frames.sh`). Nothing is blocking.
Nice to have (same style, green background, 4 × 4 grid, ≥ 2048 px for a sharp 200 % window):

| Priority | Poses | Why |
|---|---|---|
| 1 | Mouth shapes on one pose: closed, "a", "o", "i" (4 poses, same body) | Real lip-sync: today the mouth opens with the voice and the pose changes per syllable |
| 2 | Turned towards the crowd, mic held out to it (2–4 poses) | RÉPONSE: today she stands arm out while the crowd answers |
| 2 | Eyebrows: surprised / focused on the calm pose (2 poses) | The spec's eyebrows; not possible on the sprites today |
| 3 | Higher-resolution sheets | ~310 px per character today (slightly soft at 200 %) |

#include "sdna/ArtistDatabase.h"
#include "sdna/Dsp.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <sstream>
#include <cstring>

namespace sdna {

// Column order: dark distorted electronic lofi aggressive atmospheric industrial glitch vintage heavy energy melodic experimental hyper
static const std::vector<ArtistEntry> kDb = {
// ----------------------------------------------------------------- ARTISTS
{ "The Hellp", "hellp|the help", false, { 75, 85, 85, 50, 80, 40, 45, 35, 45, 55, 80, 45, 45, 25 }, 128, 0.05f, "minor", "electroclash,four_floor,breakbeat",
  "electroclash, indie sleaze, electropunk", "Blown-out, loud and dark electroclash: overdriven synths, crushed drum machines and punk energy with a French-touch compression pump.",
  "Overdriven saw/square synth stabs|Crushed, clipping drum machines|Pumping bus compression|Snotty, saturated vocal processing|Dark minor-key riffs" },
{ "Crystal Castles", "crystalcastles", false, { 75, 80, 90, 75, 70, 55, 35, 70, 50, 45, 75, 55, 55, 20 }, 125, 0.05f, "minor", "electroclash,four_floor,breakbeat",
  "chiptune, witch house, electropunk", "Chip-style square-wave leads and bitcrushed drum machines colliding with blown-out vocals and dreamy reverb.",
  "8-bit square-wave leads (sound-chip style)|Bitcrushed drum machines|Heavily distorted vocal textures|Hazy reverb pads under harsh noise|Glitchy edits" },
{ "Burial", "", false, { 85, 20, 60, 80, 25, 90, 20, 30, 50, 65, 40, 55, 45, 15 }, 138, 0.55f, "minor", "garage,halftime",
  "UK garage, future garage, dubstep", "Nocturnal, crackling 2-step: unquantised shuffled drums, vinyl and rain textures, wobbling sub and ghostly pitched vocal fragments.",
  "Shuffled, off-grid 2-step drums|Vinyl crackle and rain-like noise beds|Pitched, smeared vocal fragments|Deep wobbling sub bass|Sparse, eerie pads" },
{ "Frank Ocean", "frankocean|blonde", false, { 45, 15, 45, 35, 10, 70, 5, 15, 60, 40, 25, 85, 45, 30 }, 85, 0.2f, "major", "rnb,halftime",
  "alternative R&B, soul, art pop", "Intimate and warm: electric pianos, soft analog pads, minimal drums and pitch-shifted vocal layers with lots of air.",
  "Electric piano / Rhodes chords|Pitch-shifted vocal layers|Soft, minimal drums|Warm analog pads|Ambient guitar-like textures" },
{ "Yabujin", "", false, { 65, 75, 95, 45, 75, 65, 30, 45, 30, 75, 85, 70, 60, 70 }, 150, 0.05f, "harmonic minor", "hardstyle,trap,phonk",
  "hyper-trance, trap, phonk, vaporwave", "Hyper-saturated internet music: trance supersaws, hardstyle-style distorted kicks, blown 808s and eastern-tinged melodies.",
  "Trance supersaw leads|Hardstyle-style distorted kicks|Clipped, blown-out 808s|Harmonic-minor melodies|Chopped vocal stabs" },
{ "Aphex Twin", "richard d james|afx|aphextwin", false, { 55, 45, 90, 40, 55, 55, 35, 85, 55, 50, 70, 65, 95, 30 }, 140, 0.1f, "minor", "breakbeat,dnb,ambient",
  "IDM, acid, ambient techno, drill 'n' bass", "Hyper-edited breakbeats, acid squelch, detuned analog pads and microtonal melodies — playful and unsettling at once.",
  "Fast, hyper-edited breakbeats|Acid squelch basslines|Detuned analog pads|Microtonal / detuned melodies|Granular and glitch edits" },
{ "Nine Inch Nails", "nin|trent reznor|nineinchnails", false, { 85, 85, 65, 35, 85, 55, 95, 40, 40, 70, 65, 45, 55, 5 }, 115, 0.0f, "phrygian", "industrial,breakbeat,halftime",
  "industrial rock, industrial, darkwave", "Mechanical, abrasive industrial: crushed drum machines, metallic percussion, noise layers and distorted bass with fragile piano moments.",
  "Crushed, distorted drum machines|Metallic, sampled-object style percussion|Noise and feedback layers|Distorted synth bass|Sparse, melancholic piano" },
{ "Dean Blunt", "babyfather|deanblunt", false, { 70, 30, 45, 80, 20, 65, 25, 20, 65, 45, 20, 45, 70, 5 }, 80, 0.3f, "minor", "halftime,ambient,boom_bap",
  "hypnagogic pop, lo-fi, dub, experimental", "Murky, hazy and minimal: tape-worn drums, dubby echoes, sparse out-of-focus keys and deliberately unpolished textures.",
  "Muffled, tape-saturated drums|Dubby echo throws|Sparse, slightly detuned keys|Murky low vocal textures|Long silences and drones" },
{ "SOPHIE", "sophie xeon|sophiemsmsmsm", false, { 35, 60, 100, 5, 70, 40, 55, 45, 5, 70, 75, 55, 90, 100 }, 130, 0.0f, "major", "hyperpop,halftime,jersey",
  "hyperpop, experimental pop, deconstructed club", "Fully synthesised, plastic and metallic: rubbery latex-like percussion, huge pitch-swept bass and pitched-up vocals with extreme dynamics.",
  "Rubbery, bubbly synthesised percussion|Metallic FM hits|Huge pitch-swept bass|Pitched-up vocal formants|Extreme silence-to-loud dynamics" },
{ "Skrillex", "sonny moore", false, { 50, 90, 95, 15, 90, 35, 55, 70, 10, 85, 90, 50, 55, 55 }, 140, 0.0f, "minor", "halftime,trap,four_floor",
  "brostep, dubstep, EDM", "Maximal sound design: snarling resampled basses, huge compressed drums, vocal chops and big drop dynamics.",
  "Snarling FM / wavefolded bass|Huge compressed snares|Vocal chop hooks|Glitch edits and stutters|Big risers and impacts" },
{ "Death Grips", "deathgrips|zach hill|mc ride", false, { 80, 95, 75, 55, 100, 30, 75, 70, 30, 70, 90, 20, 85, 20 }, 130, 0.05f, "phrygian", "breakbeat,industrial,trap",
  "experimental hip hop, industrial hip hop, noise", "Brutal, chaotic and blown out: clipped live-drum chops, harsh noise, alarm-like synths and shouted vocals.",
  "Clipped, chopped drum hits|Harsh noise bursts|Alarm-like synth stabs|Extreme distortion on everything|Chaotic edits" },
{ "Arca", "", false, { 70, 75, 95, 20, 70, 60, 70, 80, 10, 65, 55, 50, 100, 60 }, 105, 0.1f, "phrygian", "reggaeton,halftime,breakbeat",
  "deconstructed club, experimental electronic, reggaeton", "Mutating, fleshy sound design: warped percussion, morphing formant textures, reggaeton fragments and extreme pitch bending.",
  "Warped, fleshy percussion|Morphing formant textures|Reggaeton rhythmic fragments|Extreme pitch bends|Metallic resonances" },
{ "Oneohtrix Point Never", "opn|daniel lopatin", false, { 55, 35, 90, 45, 30, 80, 30, 70, 70, 35, 40, 70, 90, 35 }, 100, 0.05f, "lydian", "ambient,halftime,breakbeat",
  "experimental electronic, ambient, plunderphonics", "Arpeggiated vintage synths, choir-like pads and chopped micro-samples assembled into uncanny, nostalgic collages.",
  "Arpeggiated vintage synths|Choir-like synthetic pads|Chopped micro-sample collages|MIDI-ish uncanny instruments|Granular smears" },
{ "Boards of Canada", "boc", false, { 55, 20, 65, 75, 15, 70, 5, 15, 95, 50, 30, 75, 40, 5 }, 90, 0.2f, "dorian", "boom_bap,halftime,ambient",
  "IDM, downtempo, hauntology", "Wobbly, sun-faded nostalgia: detuned tape-warbled synths, dusty hip-hop drums and pastoral melodies.",
  "Detuned, tape-warbled synth pads|Dusty hip-hop drum breaks|Analog wow and flutter|Pastoral melodies|Children's-TV style tones" },
{ "Daft Punk", "daftpunk", false, { 30, 50, 90, 20, 50, 30, 10, 20, 70, 60, 75, 75, 30, 35 }, 122, 0.1f, "dorian", "four_floor,electroclash",
  "French house, disco, electronic", "Filtered disco loops, pumping compression, vocoded voices and bright analog synths on a four-on-the-floor.",
  "Filter-swept loops|Pumping sidechain-style compression|Vocoder / talkbox voices|Bright analog leads|Tight four-on-the-floor kicks" },
{ "Justice", "", false, { 55, 85, 90, 30, 80, 35, 30, 35, 45, 65, 80, 60, 35, 25 }, 120, 0.05f, "minor", "electroclash,four_floor",
  "French electro, electro house", "Overdriven, compressed electro: distorted bass riffs, crunchy filtered disco and cathedral-sized synth chords.",
  "Overdriven bass riffs|Crunchy distorted disco drums|Big organ-like synth chords|Hard bus compression|Filter sweeps" },
{ "Massive Attack", "massiveattack", false, { 80, 30, 60, 45, 35, 80, 30, 15, 60, 75, 30, 60, 45, 5 }, 85, 0.15f, "minor", "halftime,boom_bap",
  "trip hop, downtempo", "Dark, heavy trip hop: slow breakbeats, huge dub basslines, smoky atmospheres and tense string swells.",
  "Slow heavy breakbeats|Deep dub basslines|Smoky atmospheres|Tense string swells|Scratchy vinyl textures" },
{ "Portishead", "", false, { 80, 35, 40, 75, 35, 70, 20, 15, 85, 55, 25, 70, 45, 5 }, 80, 0.2f, "minor", "boom_bap,halftime",
  "trip hop", "Noir trip hop: vinyl-scratched breaks, theremin-like leads, spy-film strings and degraded, sampled textures.",
  "Vinyl-degraded breakbeats|Theremin-like leads|Cinematic noir strings|Scratch textures|Mournful organ" },
{ "Björk", "bjork", false, { 50, 40, 80, 20, 45, 75, 40, 55, 20, 55, 50, 85, 85, 40 }, 100, 0.05f, "lydian", "halftime,breakbeat,ambient",
  "art pop, experimental, electronic", "Organic meets electronic: micro-beat clicks, brass and string swells, choirs, and inventive found-sound percussion.",
  "Clicky micro-beats|Choir and string swells|Found-sound percussion|Celesta and music-box tones|Huge dynamic contrasts" },
{ "Radiohead", "thom yorke", false, { 65, 40, 55, 40, 40, 70, 30, 50, 50, 45, 45, 75, 65, 10 }, 110, 0.05f, "dorian", "breakbeat,halftime",
  "art rock, electronic, IDM-influenced rock", "Anxious, textured art rock: glitchy drum machines, Ondes-like leads, distorted pianos and dense atmospheric layers.",
  "Glitchy drum machines|Ondes Martenot-style leads|Distorted piano|Dense atmospheric layers|Stuttering edits" },
{ "Kanye West", "ye|kanye|yeezus", false, { 55, 55, 70, 30, 65, 55, 45, 30, 50, 75, 55, 70, 55, 25 }, 90, 0.1f, "minor", "trap,boom_bap,halftime",
  "hip hop, industrial hip hop, soul", "From pitched soul chops to Yeezus-era industrial minimalism: huge drums, distorted synth stabs, choirs and 808s.",
  "Pitched soul vocal chops|Hard 808s|Distorted synth stabs|Gospel choirs|Minimal, punchy drums" },
{ "Travis Scott", "travisscott|la flame", false, { 75, 50, 85, 25, 65, 85, 25, 25, 20, 85, 55, 60, 40, 30 }, 150, 0.05f, "minor", "trap,halftime",
  "trap, psychedelic hip hop", "Psychedelic trap: dark ambient pads, huge reverbs, autotuned textures, sliding 808s and sparse metallic hats.",
  "Sliding, distorted 808s|Huge reverb atmospheres|Dark ambient pads|Autotuned vocal textures|Sparse metallic hats" },
{ "Playboi Carti", "carti|playboicarti", false, { 60, 70, 90, 35, 80, 45, 30, 40, 15, 90, 85, 55, 55, 55 }, 155, 0.0f, "minor", "trap,hyperpop",
  "rage, trap", "Rage-era trap: blown-out 808s, buzzy synth leads, fast hats and minimal, hypnotic loops.",
  "Blown-out 808s|Buzzy supersaw rage leads|Rapid hi-hat rolls|Hypnotic short loops|Pitched vocal ad-libs" },
{ "Metro Boomin", "metro", false, { 80, 40, 85, 15, 60, 65, 25, 15, 25, 90, 55, 60, 30, 15 }, 140, 0.0f, "harmonic minor", "trap,halftime",
  "trap, hip hop", "Dark cinematic trap: huge clean 808s, sinister bells and plucks, orchestral swells and crisp hats.",
  "Huge clean 808s|Sinister bells and plucks|Orchestral swells|Crisp rolled hi-hats|Dark minor melodies" },
{ "J Dilla", "jay dee|dilla", false, { 35, 30, 35, 70, 30, 30, 5, 10, 90, 55, 40, 75, 35, 5 }, 90, 0.65f, "dorian", "boom_bap,rnb",
  "hip hop, neo soul", "Drunken, swung boom bap: unquantised drums, dusty soul chops and warm Rhodes over deep bass.",
  "Unquantised 'drunk' swing|Dusty sampled-style drums|Warm Rhodes chords|Soul vocal chops|Deep warm bass" },
{ "Madlib", "quasimoto|madvillain", false, { 45, 35, 30, 85, 30, 35, 10, 20, 95, 50, 35, 70, 55, 10 }, 88, 0.45f, "dorian", "boom_bap",
  "hip hop, jazz rap, beat music", "Crate-dug and crusty: lo-fi drum breaks, jazz and psych fragments, tape saturation and loose loops.",
  "Crusty lo-fi drum breaks|Jazzy organ and keys|Heavy tape saturation|Loose, unpolished loops|Odd vocal snippets" },
{ "MF DOOM", "doom|madvillain|mfdoom", false, { 50, 35, 35, 75, 35, 30, 10, 15, 90, 55, 35, 65, 45, 10 }, 92, 0.35f, "minor", "boom_bap",
  "hip hop, underground rap", "Grimy boom bap: dusty breaks, cartoon and soul fragments, loose swing and crunchy low-bit sampling.",
  "Dusty boom-bap breaks|Low-bit sampler crunch|Loose swing|Cartoonish melodic fragments|Warm bass" },
{ "Flying Lotus", "flylo|flyinglotus", false, { 50, 35, 75, 55, 35, 70, 20, 50, 55, 65, 50, 80, 75, 25 }, 85, 0.45f, "dorian", "boom_bap,halftime,breakbeat",
  "beat music, jazz fusion, electronic", "Cosmic, wobbly beat music: swung lo-fi drums, warped jazz chords, sidechained bass and psychedelic textures.",
  "Swung, wonky drums|Warped jazz chords|Sidechained bass|Psychedelic textures|Harp and keys" },
{ "Four Tet", "kieran hebden|fourtet", false, { 35, 20, 70, 40, 30, 65, 5, 35, 50, 45, 55, 80, 55, 15 }, 128, 0.25f, "major", "garage,four_floor,breakbeat",
  "folktronica, house, UK garage", "Warm, intricate and organic-electronic: chopped percussion, harp and folk samples, shuffled house grooves.",
  "Chopped organic percussion|Harp and plucked tones|Shuffled house grooves|Warm pads|Granular vocal snippets" },
{ "Jamie xx", "jamiexx", false, { 45, 20, 70, 35, 25, 65, 10, 25, 55, 50, 55, 80, 40, 20 }, 120, 0.2f, "major", "garage,four_floor",
  "UK garage, house, electronic", "Melancholy dancefloor: steel-pan-like tones, swung garage drums, warm sub and nostalgic vocal snippets.",
  "Steel drum / mallet tones|Swung garage drums|Warm round sub|Nostalgic vocal snippets|Sparse reverb-soaked chords" },
{ "Fred again..", "fred again|fredagain", false, { 30, 25, 80, 30, 45, 60, 5, 30, 25, 60, 70, 80, 30, 35 }, 130, 0.15f, "major", "garage,four_floor,breakbeat",
  "house, UK garage, electronic", "Euphoric, emotional dance: chopped voice-memo vocals, big side-chained chords, punchy garage-house drums.",
  "Chopped vocal hooks|Big sidechained chords|Punchy garage-house drums|Piano stabs|Lo-fi found textures" },
{ "Bladee", "drain gang|draingang|ecco2k|whitearmor", false, { 45, 30, 95, 30, 25, 80, 10, 30, 15, 55, 45, 80, 55, 75 }, 140, 0.0f, "major", "trap,hyperpop,ambient",
  "cloud rap, hyperpop", "Glittering, icy cloud rap: trance-like pads, crystalline bells, soft 808s and heavily tuned vocals.",
  "Crystalline bells and arps|Trance-like airy pads|Soft round 808s|Heavily tuned vocals|Icy reverbs" },
{ "Yung Lean", "yunglean|sadboys", false, { 60, 30, 85, 50, 25, 80, 10, 20, 35, 65, 35, 75, 35, 30 }, 130, 0.0f, "minor", "trap,halftime",
  "cloud rap", "Melancholic cloud rap: hazy pads, video-game-like melodies, booming 808s and washed-out reverb.",
  "Hazy wide pads|Video-game-like melodies|Booming 808s|Washed-out reverbs|Sad minor chords" },
{ "100 gecs", "100gecs|dylan brady|laura les", false, { 30, 95, 95, 30, 90, 25, 40, 75, 15, 75, 95, 60, 80, 100 }, 160, 0.0f, "major", "hyperpop,dnb,trap",
  "hyperpop, digicore", "Maximally clipped hyperpop: chipmunk vocals, dubstep wobbles, ska and metal fragments, everything distorted and pitched up.",
  "Clipped everything|Chipmunk-pitched vocals|Dubstep wobble bass|Genre-whiplash edits|Bright supersaws" },
{ "Charli XCX", "charli|brat", false, { 35, 60, 95, 15, 65, 35, 30, 35, 10, 70, 85, 65, 50, 85 }, 128, 0.0f, "major", "four_floor,hyperpop",
  "pop, hyperpop, electropop", "Glossy club pop with grit: metallic synth stabs, punchy club drums, autotuned vocals and distorted basses.",
  "Metallic synth stabs|Punchy club drums|Autotuned vocal chops|Distorted bass|Hyper-glossy production" },
{ "A. G. Cook", "ag cook|agcook|pc music", false, { 25, 50, 100, 10, 55, 40, 35, 45, 10, 55, 80, 70, 80, 100 }, 130, 0.0f, "major", "hyperpop,four_floor,jersey",
  "hyperpop, PC Music, bubblegum bass", "Saccharine, plastic and uncanny: glassy synths, bubblegum percussion, pitched vocals and abrupt dynamic shifts.",
  "Glassy, plastic synths|Bubblegum percussion|Pitched-up vocals|Abrupt dynamic shifts|Trance-like chords" },
{ "Grimes", "", false, { 50, 40, 90, 35, 40, 70, 25, 45, 35, 55, 60, 75, 70, 55 }, 115, 0.0f, "minor", "four_floor,halftime,breakbeat",
  "art pop, synth-pop, electronic", "Ethereal DIY synth-pop: layered falsetto vocals, glassy arps, booming drums and dark-fairytale atmospheres.",
  "Layered ethereal vocals|Glassy arpeggios|Booming drums|Dark fairytale pads|Lo-fi DIY textures" },
{ "The Weeknd", "weeknd|abel", false, { 65, 25, 80, 20, 35, 75, 10, 10, 65, 60, 55, 80, 25, 20 }, 110, 0.05f, "minor", "rnb,four_floor,halftime",
  "R&B, synth-pop", "Neon-noir R&B: 80s-style synths and gated drums, dark reverb-drenched pads and smooth bass.",
  "80s gated drums|Neon analog synths|Reverb-drenched pads|Smooth synth bass|Dark melodic hooks" },
{ "Drake", "40|noah 40 shebib|ovo", false, { 60, 15, 75, 25, 25, 75, 5, 10, 30, 70, 35, 75, 20, 10 }, 140, 0.0f, "minor", "trap,rnb,halftime",
  "hip hop, R&B", "Underwater, moody minimalism: low-passed samples, deep 808s, sparse drums and submerged pads.",
  "Low-passed, underwater pads|Deep 808s|Sparse drums|Submerged piano|Moody minimalism" },
{ "Tame Impala", "kevin parker|tameimpala", false, { 35, 40, 55, 35, 30, 75, 5, 15, 85, 55, 50, 85, 40, 20 }, 115, 0.1f, "dorian", "breakbeat,four_floor",
  "psychedelic pop, synth-pop", "Phased, compressed psychedelia: heavy-compressed drums, phased synths, lush chords and warm vintage bass.",
  "Heavily compressed drums|Phaser-soaked synths|Lush vintage chords|Warm round bass|Flanged textures" },
{ "Billie Eilish", "finneas|billie", false, { 70, 30, 75, 20, 30, 60, 20, 25, 20, 80, 35, 65, 50, 30 }, 120, 0.05f, "minor", "halftime,trap",
  "pop, dark pop", "Intimate dark pop: whispered close vocals, huge distorted sub drops, found-sound percussion and quiet-loud contrast.",
  "Whisper-close vocal textures|Huge distorted sub drops|Found-sound percussion clicks|Sparse piano|Quiet-loud contrast" },
{ "Lana Del Rey", "lana", false, { 55, 10, 35, 40, 10, 80, 5, 5, 85, 45, 20, 80, 20, 5 }, 75, 0.15f, "minor", "halftime,rnb",
  "baroque pop, dream pop", "Cinematic Americana noir: lush strings, slow hip-hop drums, tremolo guitars and vintage reverb.",
  "Lush cinematic strings|Slow hip-hop drums|Vintage plate reverb|Tremolo guitar-like tones|Melancholy piano" },
{ "Kraftwerk", "", false, { 30, 10, 100, 10, 30, 20, 50, 10, 90, 35, 55, 65, 50, 15 }, 115, 0.0f, "minor", "four_floor,electroclash",
  "electronic, synth-pop, krautrock", "Clean, precise machine music: analog drum machines, vocoders, sequenced bass and minimalist melodies.",
  "Clean analog drum machines|Vocoder voices|Sequenced bass|Minimalist melodies|Mechanical precision" },
{ "Depeche Mode", "depechemode", false, { 75, 35, 85, 15, 45, 55, 55, 15, 70, 55, 55, 70, 30, 5 }, 115, 0.0f, "minor", "four_floor,industrial",
  "synth-pop, darkwave", "Dark synth-pop: sampled metallic percussion, brooding analog bass, choir pads and gothic melodies.",
  "Sampled metallic percussion|Brooding analog bass|Choir-like pads|Gothic melodies|Industrial textures" },
{ "The Prodigy", "prodigy", false, { 60, 80, 85, 30, 95, 30, 50, 45, 45, 75, 95, 40, 40, 20 }, 140, 0.05f, "phrygian", "breakbeat,dnb",
  "big beat, breakbeat, rave", "Rave aggression: distorted breakbeats, acid stabs, siren-like leads and punk energy.",
  "Distorted breakbeats|Acid stabs|Siren-like leads|Punk energy|Big riffs" },
{ "The Chemical Brothers", "chemical brothers", false, { 45, 65, 85, 30, 70, 45, 40, 40, 55, 70, 85, 55, 50, 20 }, 128, 0.1f, "dorian", "breakbeat,four_floor",
  "big beat, electronic", "Psychedelic big beat: huge compressed breaks, squelchy acid basslines and swirling filtered loops.",
  "Huge compressed breaks|Squelchy acid basslines|Swirling filtered loops|Phased synths|Big risers" },
{ "Autechre", "ae", false, { 70, 45, 100, 25, 50, 55, 70, 90, 30, 50, 60, 40, 100, 20 }, 120, 0.1f, "phrygian", "breakbeat,industrial,ambient",
  "IDM, experimental electronic", "Abstract algorithmic rhythm: metallic clicks, generative sequences, alien FM tones and shifting meters.",
  "Metallic clicks and pops|Generative rhythms|Alien FM tones|Shifting meters|Cold reverbs" },
{ "Squarepusher", "tom jenkinson", false, { 45, 50, 85, 30, 70, 40, 35, 85, 40, 60, 95, 65, 85, 25 }, 170, 0.05f, "dorian", "dnb,breakbeat",
  "drill 'n' bass, IDM, jazz fusion", "Hyperactive drill 'n' bass: rapid-fire edited breaks, slap-bass-like synth lines and acid squelch.",
  "Rapid-fire edited breaks|Slap-bass-like synth lines|Acid squelch|Jazz chords|Fast glitch fills" },
{ "Venetian Snares", "aaron funk", false, { 70, 75, 85, 35, 90, 45, 50, 95, 30, 60, 100, 55, 85, 20 }, 180, 0.0f, "harmonic minor", "dnb,breakbeat",
  "breakcore", "Breakcore brutality: hyper-edited Amen-style breaks in odd meters, distorted kicks and classical string fragments.",
  "Hyper-edited breaks|Odd meters|Distorted kicks|Classical string fragments|Extreme glitching" },
{ "Clams Casino", "clamscasino", false, { 55, 25, 70, 55, 25, 90, 10, 20, 40, 70, 35, 75, 45, 20 }, 140, 0.0f, "minor", "trap,halftime",
  "cloud rap, instrumental hip hop", "Cloud rap blueprint: washed-out pitched vocal pads, heavy reverb, slow drums and deep 808s.",
  "Washed-out vocal pads|Heavy reverbs|Slow heavy drums|Deep 808s|Dreamy filtered textures" },
{ "SALEM", "salem band", false, { 95, 70, 80, 70, 55, 80, 50, 35, 40, 85, 30, 45, 55, 15 }, 70, 0.0f, "minor", "halftime,trap",
  "witch house", "Witch house: slowed-down chopped-and-screwed tempos, occult choirs, crushed 808s and murky distortion.",
  "Slowed, screwed tempos|Occult choir pads|Crushed 808s|Murky distortion|Pitched-down vocals" },
{ "HEALTH", "health band", false, { 85, 90, 75, 30, 90, 50, 85, 50, 20, 80, 75, 35, 55, 15 }, 110, 0.0f, "phrygian", "industrial,halftime",
  "noise rock, industrial", "Crushing industrial noise rock: huge distorted drums, shredded synth noise and ethereal vocals over brutal low end.",
  "Huge distorted drums|Shredded synth noise|Ethereal vocal textures|Brutal low end|Metallic hits" },
{ "Gesaffelstein", "", false, { 90, 65, 95, 15, 80, 45, 80, 25, 30, 70, 70, 40, 35, 5 }, 125, 0.0f, "phrygian", "four_floor,industrial,electroclash",
  "dark techno, industrial techno", "Cold, menacing techno: metallic drums, distorted acid-like bass, dark synth stabs and mechanical precision.",
  "Metallic drums|Distorted acid-like bass|Dark synth stabs|Mechanical precision|Cold reverbs" },
{ "Kavinsky", "", false, { 60, 40, 90, 20, 50, 50, 15, 10, 90, 60, 65, 70, 20, 10 }, 115, 0.0f, "minor", "four_floor,electroclash",
  "synthwave, French electro", "Retro night-drive synthwave: gated 80s drums, analog brass leads, pulsing bass arps.",
  "Gated 80s drums|Analog brass leads|Pulsing bass arpeggios|Neon pads|Retro reverbs" },
{ "Perturbator", "", false, { 85, 60, 95, 15, 70, 60, 45, 20, 75, 65, 80, 65, 30, 10 }, 120, 0.0f, "phrygian", "four_floor,industrial",
  "darksynth, synthwave", "Darksynth: distorted 80s synths, gated drums, menacing arpeggios and cyberpunk atmosphere.",
  "Distorted retro synths|Gated drums|Menacing arpeggios|Cyberpunk atmospheres|Big brass stabs" },
{ "Jon Hopkins", "jonhopkins", false, { 45, 40, 85, 25, 45, 80, 25, 45, 25, 65, 60, 85, 50, 15 }, 124, 0.05f, "major", "four_floor,ambient,breakbeat",
  "electronica, ambient techno", "Emotional, textural techno: crunchy compressed percussion, swelling granular pads and piano.",
  "Crunchy compressed percussion|Swelling granular pads|Piano motifs|Deep pulses|Rich textures" },
{ "Bonobo", "", false, { 35, 15, 60, 35, 20, 70, 5, 15, 50, 50, 40, 85, 25, 10 }, 110, 0.25f, "dorian", "breakbeat,four_floor,rnb",
  "downtempo, electronica", "Lush organic downtempo: live-feel drums, strings, woodwind-like tones and warm bass.",
  "Live-feel drums|Lush strings|Woodwind-like tones|Warm bass|Organic percussion" },
{ "Nujabes", "", false, { 30, 10, 35, 65, 10, 55, 0, 5, 85, 45, 35, 90, 15, 5 }, 90, 0.4f, "dorian", "boom_bap",
  "jazz hop, lo-fi hip hop", "Jazzy, nostalgic boom bap: dusty swung drums, piano and flute-like melodies, warm bass.",
  "Dusty swung drums|Jazz piano|Flute-like melodies|Warm upright-like bass|Vinyl crackle" },
{ "Tyler, the Creator", "tyler the creator|tyler", false, { 40, 45, 65, 35, 50, 55, 15, 20, 70, 70, 55, 85, 55, 25 }, 95, 0.2f, "dorian", "boom_bap,rnb,trap",
  "hip hop, neo soul, funk", "Colorful and crunchy: jazzy chords, distorted synth bass, Neptunes-like drums and lush pads.",
  "Jazzy extended chords|Distorted synth bass|Neptunes-style drums|Lush vintage pads|Bright synth leads" },
{ "Kaytranada", "", false, { 35, 25, 75, 30, 35, 40, 5, 20, 55, 65, 65, 75, 30, 15 }, 110, 0.45f, "dorian", "rnb,four_floor,boom_bap",
  "house, R&B, hip hop", "Bouncy, swung house-R&B: syncopated drums, round fat bass and warm filtered chords.",
  "Syncopated swung drums|Round fat synth bass|Warm filtered chords|Shakers and congas|Bouncy grooves" },
{ "Flume", "", false, { 35, 50, 95, 15, 55, 55, 30, 60, 15, 65, 65, 75, 60, 55 }, 150, 0.1f, "major", "halftime,trap",
  "future bass, electronic", "Warped future bass: chopped vocal hooks, wobbling saw chords, crunchy percussion and pitched textures.",
  "Wobbling saw chords|Chopped vocal hooks|Crunchy percussion|Pitched textures|Big detuned synths" },
{ "Porter Robinson", "virtual self", false, { 30, 30, 95, 15, 40, 75, 10, 30, 25, 55, 70, 90, 35, 60 }, 128, 0.0f, "major", "four_floor,hyperpop,halftime",
  "electronic, future bass, trance", "Cinematic, emotional electronica: huge supersaw chords, sparkling arps, tuned vocals and big reverbs.",
  "Huge supersaw chords|Sparkling arpeggios|Tuned vocals|Huge reverbs|Bright bells" },
{ "deadmau5", "deadmau5|joel zimmerman", false, { 45, 30, 95, 10, 45, 50, 25, 15, 30, 65, 75, 70, 30, 10 }, 128, 0.0f, "minor", "four_floor",
  "progressive house, electro house", "Precise progressive house: long filter sweeps, sidechained pads, clean tight drums and plucky leads.",
  "Long filter sweeps|Sidechained pads|Clean tight drums|Plucky leads|Rolling bass" },
{ "Kode9", "", false, { 85, 30, 75, 40, 40, 70, 40, 35, 40, 85, 50, 40, 60, 10 }, 140, 0.25f, "phrygian", "halftime,garage",
  "dubstep, UK bass", "Dark, sparse dubstep: deep sub pressure, eerie melodies and syncopated half-time drums.",
  "Deep sub pressure|Eerie minimal melodies|Syncopated half-time drums|Dub echoes|Dark atmospheres" },
{ "Actress", "darren cunningham", false, { 75, 40, 75, 80, 30, 65, 45, 50, 60, 55, 50, 40, 85, 5 }, 125, 0.15f, "minor", "four_floor,ambient",
  "outsider house, experimental techno", "Corroded, murky outsider techno: degraded drum machines, smeared pads and blurred loops.",
  "Degraded drum machines|Smeared pads|Blurred loops|Murky low end|Corroded textures" },
{ "Andy Stott", "", false, { 90, 45, 70, 60, 40, 75, 55, 30, 45, 90, 35, 40, 65, 5 }, 115, 0.1f, "minor", "halftime,industrial",
  "dub techno, experimental", "Heavy, slow and murky: crushing sub, slowed-down techno drums, ghostly voices and dense fog.",
  "Crushing sub|Slowed techno drums|Ghostly vocals|Dense fog textures|Tape saturation" },
{ "Tim Hecker", "", false, { 65, 55, 70, 50, 30, 100, 35, 40, 40, 50, 10, 60, 85, 5 }, 70, 0.0f, "minor", "ambient",
  "ambient, drone, noise", "Saturated ambient drones: distorted organ-like swells, granular wash and decaying textures.",
  "Distorted organ-like drones|Granular wash|Decaying textures|Massive reverbs|Noise beds" },
{ "William Basinski", "basinski", false, { 55, 15, 35, 85, 5, 95, 10, 25, 95, 30, 5, 65, 60, 0 }, 60, 0.0f, "major", "ambient",
  "ambient, tape music", "Disintegrating tape loops: slowly decaying melodic fragments with heavy tape wear and huge space.",
  "Decaying tape loops|Heavy tape wear|Melodic fragments|Huge space|Hiss and dropouts" },
{ "Brian Eno", "eno", false, { 30, 5, 60, 20, 5, 90, 5, 10, 70, 35, 10, 75, 55, 0 }, 70, 0.0f, "major", "ambient",
  "ambient, art rock", "Generative ambience: slow evolving pads, piano with long tails and gentle tape processing.",
  "Slow evolving pads|Piano with long tails|Gentle tape delays|Generative patterns|Soft bells" },
{ "Vangelis", "", false, { 40, 10, 85, 10, 20, 90, 10, 5, 90, 50, 25, 90, 30, 5 }, 90, 0.0f, "minor", "ambient,halftime",
  "electronic, film score", "Cinematic analog grandeur: CS-80-style brass, huge reverb, bells and sweeping pads.",
  "CS-80-style brass|Huge reverbs|Bells|Sweeping pads|Big timpani-like toms" },
{ "John Carpenter", "carpenter", false, { 80, 25, 85, 30, 40, 60, 25, 5, 90, 55, 45, 60, 25, 0 }, 110, 0.0f, "phrygian", "four_floor,halftime",
  "horror score, synthwave", "Minimal horror synths: pulsing analog bass ostinatos, eerie pads and stark piano motifs.",
  "Pulsing bass ostinatos|Eerie analog pads|Stark piano motifs|Synth brass|Tension risers" },
{ "clipping.", "clipping|daveed diggs", false, { 80, 70, 70, 30, 75, 45, 90, 60, 20, 65, 65, 20, 90, 5 }, 120, 0.05f, "phrygian", "industrial,trap",
  "experimental hip hop, noise", "Noise-rap: harsh field-recording-like noises, industrial percussion, sirens and sparse sub.",
  "Harsh noise percussion|Industrial clanks|Sirens and alarms|Sparse sub|Abrasive textures" },
{ "JPEGMAFIA", "peggy|jpegmafia", false, { 55, 75, 80, 45, 80, 45, 50, 80, 30, 75, 75, 55, 85, 40 }, 140, 0.15f, "minor", "trap,breakbeat",
  "experimental hip hop", "Chaotic collage rap: glitched, chopped samples, blown-out drums, sudden switch-ups and warm chord fragments.",
  "Glitched chopped samples|Blown-out drums|Sudden switch-ups|Warm chord fragments|Noise bursts" },
{ "Lil Peep", "", false, { 70, 45, 60, 55, 35, 75, 10, 15, 40, 70, 40, 75, 25, 15 }, 140, 0.0f, "minor", "trap,halftime",
  "emo rap, cloud rap", "Emo-trap: lo-fi guitar-like loops, washed-out reverb, booming 808s and melancholic melodies.",
  "Lo-fi guitar-like loops|Washed-out reverbs|Booming 808s|Melancholic melodies|Hazy textures" },
{ "$uicideboy$", "suicideboys|uicideboy", false, { 90, 65, 75, 60, 75, 55, 40, 25, 45, 90, 60, 40, 30, 5 }, 140, 0.0f, "phrygian", "phonk,trap",
  "phonk, horrorcore, trap", "Dark Southern phonk-trap: distorted 808s, Memphis cowbells, horror bells and murky lo-fi textures.",
  "Distorted 808s|Memphis-style cowbells|Horror bells|Murky lo-fi textures|Dark choir pads" },
{ "Ghostemane", "", false, { 95, 85, 70, 45, 95, 40, 80, 40, 30, 90, 80, 25, 45, 5 }, 150, 0.0f, "phrygian", "trap,industrial,phonk",
  "industrial hip hop, trap metal", "Trap metal: blown-out 808s, industrial noise, screamed vocal textures and dark phonk cowbells.",
  "Blown-out 808s|Industrial noise|Screamed vocal textures|Phonk cowbells|Black-metal-like distortion" },
{ "Ken Carson", "opium|destroy lonely|kencarson", false, { 70, 85, 95, 30, 85, 50, 35, 40, 15, 90, 90, 50, 45, 50 }, 160, 0.0f, "minor", "trap,hyperpop",
  "rage, trap", "Rage: distorted supersaw leads, massive clipped 808s, rapid hats and dark sci-fi atmospheres.",
  "Distorted supersaw leads|Massive clipped 808s|Rapid hats|Dark sci-fi atmospheres|Bitcrushed synths" },
{ "The Neptunes", "pharrell|neptunes|chad hugo", false, { 25, 30, 75, 15, 55, 25, 15, 15, 45, 60, 70, 65, 50, 30 }, 100, 0.2f, "dorian", "boom_bap,rnb",
  "hip hop, R&B, pop", "Sparse, bouncy and crisp: hard-panned knocking drums, quirky synths, clavinet-like keys and odd percussion.",
  "Sparse knocking drums|Quirky synth hooks|Clavinet-like keys|Odd percussion|Crisp clean mix" },
{ "Timbaland", "timbo", false, { 45, 30, 80, 20, 55, 40, 20, 40, 40, 70, 65, 60, 70, 30 }, 100, 0.25f, "phrygian", "rnb,breakbeat",
  "hip hop, R&B, pop", "Stuttering syncopation: beatboxed-style percussion, vocal chops, eastern melodies and tight swung drums.",
  "Stuttering syncopated drums|Beatbox-like percussion|Vocal chops|Eastern-tinged melodies|Tight swing" },
{ "Dr. Dre", "dre|dr dre", false, { 45, 20, 60, 15, 55, 35, 5, 5, 60, 75, 50, 70, 15, 10 }, 93, 0.15f, "minor", "boom_bap",
  "G-funk, West Coast hip hop", "Polished G-funk: hard clean drums, whiny portamento leads, deep bass and piano stabs.",
  "Hard clean drums|Whiny portamento leads|Deep synth bass|Piano stabs|Live-feel percussion" },
{ "James Blake", "jamesblake", false, { 60, 25, 70, 25, 20, 80, 10, 35, 30, 85, 30, 80, 60, 30 }, 70, 0.1f, "minor", "halftime,garage",
  "post-dubstep, electronic soul", "Fragile and subby: pitched vocal harmonies, deep sub swells, sparse clicky drums and reverb-drenched piano.",
  "Pitched vocal harmonies|Deep sub swells|Sparse clicky drums|Reverb-drenched piano|Silence as texture" },
{ "Bon Iver", "justin vernon|boniver", false, { 40, 35, 55, 40, 20, 80, 15, 50, 40, 50, 30, 85, 70, 25 }, 85, 0.05f, "major", "halftime,ambient",
  "indie folk, experimental", "Processed folk: harmonised vocoder-like voices, glitched saxophone-like tones and warm, broken textures.",
  "Harmonised vocoder-like voices|Glitched textures|Warm broken textures|Folk-like tones|Huge reverbs" },
{ "Cocteau Twins", "cocteau", false, { 35, 20, 50, 30, 15, 95, 5, 5, 85, 40, 35, 85, 40, 15 }, 110, 0.0f, "major", "halftime,four_floor",
  "dream pop, ethereal wave", "Ethereal dream pop: shimmering chorused guitar-like textures, drum machines and huge reverbs.",
  "Shimmering chorused textures|Drum machines|Huge reverbs|Ethereal voices|Bell-like tones" },
{ "My Bloody Valentine", "mbv", false, { 45, 85, 30, 50, 55, 85, 20, 35, 70, 60, 55, 75, 55, 10 }, 120, 0.0f, "major", "breakbeat,halftime",
  "shoegaze", "Shoegaze haze: walls of pitch-wobbling distortion, buried voices and blurred drums.",
  "Walls of distortion|Pitch-wobbling textures|Buried voices|Blurred drums|Huge reverse reverbs" },
{ "Joy Division", "joydivision", false, { 85, 40, 30, 50, 50, 60, 35, 10, 85, 45, 55, 50, 35, 0 }, 130, 0.0f, "minor", "breakbeat,four_floor",
  "post-punk", "Cold post-punk: tight robotic drums with gated room, melodic bass, sparse eerie synths.",
  "Tight robotic drums|Gated room reverb|Melodic bass lines|Sparse eerie synths|Cold atmosphere" },
{ "Bad Bunny", "benito|badbunny", false, { 40, 30, 85, 15, 50, 45, 10, 20, 30, 75, 75, 70, 35, 30 }, 92, 0.05f, "minor", "reggaeton,trap",
  "reggaeton, Latin trap", "Modern reggaeton: dembow kick-snare patterns, deep 808s, airy synths and percussion.",
  "Dembow rhythm|Deep 808s|Airy synths|Latin percussion|Vocal FX" },
{ "FKA twigs", "fka twigs|twigs", false, { 70, 40, 85, 20, 40, 75, 45, 55, 15, 75, 40, 70, 80, 40 }, 90, 0.05f, "minor", "halftime,rnb",
  "alternative R&B, art pop", "Avant R&B: metallic percussion, cavernous sub drops, pitched vocal manipulation and tension.",
  "Metallic percussion|Cavernous sub drops|Pitched vocal manipulation|Fragile pads|Tension and space" },
{ "Lorn", "", false, { 90, 65, 85, 45, 60, 70, 50, 35, 35, 85, 40, 55, 50, 5 }, 85, 0.05f, "phrygian", "halftime,industrial",
  "dark electronic, wonky", "Brooding cinematic bass music: grinding distorted bass, heavy half-time drums and dystopian pads.",
  "Grinding distorted bass|Heavy half-time drums|Dystopian pads|Distorted leads|Dark ambience" },
{ "Floating Points", "", false, { 30, 20, 70, 25, 25, 70, 10, 25, 60, 45, 55, 90, 55, 10 }, 122, 0.2f, "lydian", "four_floor,breakbeat",
  "house, jazz, electronic", "Jazz-tinged electronic: modular arpeggios, warm analog chords, live-feel drums and evolving textures.",
  "Modular arpeggios|Warm analog chords|Live-feel drums|Evolving textures|Celesta tones" },
// ----------------------------------------------------------------- GENRES
{ "Trap", "trap music", true, { 65, 45, 85, 15, 60, 55, 15, 20, 15, 85, 60, 55, 20, 20 }, 140, 0.0f, "minor", "trap,halftime", "trap",
  "Long tuned 808s, crisp rolled hi-hats, snappy snares on 3 and dark bells/plucks.", "808s|Hi-hat rolls|Snappy snares|Dark bells" },
{ "Drill", "uk drill|ny drill|chicago drill", true, { 85, 45, 85, 15, 70, 50, 20, 20, 15, 90, 65, 50, 20, 10 }, 142, 0.0f, "harmonic minor", "drill,trap", "drill",
  "Sliding 808s, syncopated off-beat snares, triplet hats and sinister strings/pianos.", "Sliding 808s|Syncopated snares|Triplet hats|Dark strings" },
{ "Phonk", "drift phonk|memphis phonk", true, { 80, 75, 70, 55, 70, 40, 30, 20, 50, 85, 75, 45, 25, 10 }, 135, 0.05f, "phrygian", "phonk,trap", "phonk",
  "Memphis cowbells, distorted 808s, lo-fi chopped vocals and aggressive drift energy.", "Cowbells|Distorted 808s|Chopped vocals|Lo-fi grit" },
{ "Hyperpop", "digicore|bubblegum bass", true, { 30, 75, 100, 20, 70, 35, 35, 60, 5, 70, 90, 65, 70, 100 }, 155, 0.0f, "major", "hyperpop,trap,dnb", "hyperpop",
  "Clipped, glossy and pitched-up: supersaws, metallic hits, chipmunk vocals and extreme compression.", "Supersaws|Metallic hits|Pitched vocals|Clipping" },
{ "Industrial", "industrial music|ebm|electronic body music", true, { 85, 80, 75, 35, 85, 45, 100, 35, 40, 70, 65, 30, 55, 5 }, 120, 0.0f, "phrygian", "industrial,four_floor", "industrial",
  "Metallic percussion, distorted drum machines, noise and mechanical sequenced bass.", "Metallic percussion|Distorted drum machines|Noise|Sequenced bass" },
{ "Techno", "berlin techno", true, { 65, 35, 95, 15, 55, 50, 50, 20, 30, 70, 75, 30, 35, 5 }, 130, 0.0f, "minor", "four_floor,industrial", "techno",
  "Driving four-on-the-floor, rumbling kicks, metallic hats and hypnotic synth stabs.", "Rumbling kicks|Metallic hats|Hypnotic stabs|Rolling bass" },
{ "Industrial Techno", "hard techno", true, { 85, 75, 95, 25, 85, 45, 90, 30, 25, 80, 85, 20, 45, 5 }, 140, 0.0f, "phrygian", "four_floor,industrial", "industrial techno",
  "Distorted rumble kicks, harsh metallic percussion, noise and relentless energy.", "Distorted kicks|Harsh metal percussion|Noise|Rumble" },
{ "House", "deep house|chicago house", true, { 30, 15, 85, 25, 35, 45, 5, 10, 55, 60, 65, 70, 15, 15 }, 124, 0.25f, "dorian", "four_floor,garage", "house",
  "Swung four-on-the-floor, organ and piano chords, warm bass and shakers.", "Swung grooves|Piano/organ chords|Warm bass|Shakers" },
{ "UK Garage", "garage|2-step|2step|future garage", true, { 45, 20, 80, 35, 35, 55, 5, 25, 45, 65, 60, 70, 25, 20 }, 134, 0.5f, "minor", "garage", "UK garage",
  "Shuffled 2-step rhythms, pitched vocal chops, warm subs and organ stabs.", "2-step shuffle|Vocal chops|Warm sub|Organ stabs" },
{ "Dubstep", "classic dubstep|deep dubstep", true, { 80, 40, 80, 30, 50, 60, 35, 25, 30, 95, 45, 30, 40, 5 }, 140, 0.05f, "phrygian", "halftime,garage", "dubstep",
  "Half-time drums at 140, deep wobbling sub pressure and dark sparse atmospheres.", "Half-time drums|Wobble sub|Dark atmospheres" },
{ "Riddim", "brostep|tearout", true, { 60, 95, 95, 15, 95, 25, 60, 50, 10, 95, 85, 20, 50, 30 }, 150, 0.0f, "minor", "halftime", "riddim, brostep",
  "Snarling, resampled mid basses, compressed snares and repetitive aggressive drops.", "Snarling basses|Compressed snares|Big drops" },
{ "Drum and Bass", "dnb|drum n bass|jungle|liquid", true, { 55, 45, 85, 30, 70, 50, 25, 35, 35, 85, 95, 55, 30, 15 }, 174, 0.05f, "minor", "dnb,breakbeat", "drum and bass, jungle",
  "Fast chopped breakbeats at 170+, rolling Reese basses and atmospheric pads.", "Fast breakbeats|Reese bass|Atmospheric pads" },
{ "Breakcore", "", true, { 65, 80, 85, 45, 90, 40, 45, 95, 30, 65, 100, 45, 80, 30 }, 180, 0.0f, "harmonic minor", "dnb,breakbeat", "breakcore",
  "Hyper-edited breaks, extreme distortion and chaotic glitch fills.", "Edited breaks|Extreme distortion|Glitch fills" },
{ "IDM", "intelligent dance music|braindance", true, { 50, 35, 95, 30, 45, 60, 35, 85, 40, 50, 60, 65, 90, 20 }, 120, 0.1f, "lydian", "breakbeat,ambient", "IDM",
  "Intricate edited rhythms, glitch, detuned synths and experimental sound design.", "Intricate rhythms|Glitch|Detuned synths" },
{ "Ambient", "drone|ambient music", true, { 45, 10, 65, 25, 5, 100, 10, 15, 50, 40, 5, 70, 45, 5 }, 70, 0.0f, "major", "ambient", "ambient",
  "Long evolving pads, drones, granular textures and huge reverbs.", "Evolving pads|Drones|Granular textures|Huge reverbs" },
{ "Dark Ambient", "", true, { 95, 30, 65, 40, 20, 100, 55, 25, 40, 70, 5, 30, 60, 0 }, 60, 0.0f, "phrygian", "ambient", "dark ambient",
  "Ominous drones, metallic resonances, sub rumbles and vast dark spaces.", "Ominous drones|Metallic resonances|Sub rumbles" },
{ "Synthwave", "retrowave|outrun", true, { 45, 25, 90, 15, 40, 55, 10, 5, 95, 55, 60, 75, 15, 10 }, 110, 0.0f, "minor", "four_floor", "synthwave",
  "Gated 80s drums, analog arps, brass leads and lush chorused pads.", "Gated drums|Analog arps|Brass leads|Chorused pads" },
{ "Darkwave", "coldwave|darksynth", true, { 85, 45, 85, 30, 55, 60, 45, 15, 75, 55, 60, 60, 30, 5 }, 125, 0.0f, "minor", "four_floor,electroclash", "darkwave",
  "Cold drum machines, brooding analog bass and gothic synth melodies.", "Cold drum machines|Analog bass|Gothic melodies" },
{ "Witch House", "witchhouse|drag", true, { 95, 60, 80, 65, 45, 85, 45, 35, 40, 85, 25, 45, 55, 15 }, 70, 0.0f, "minor", "halftime,trap", "witch house",
  "Slowed tempos, occult choirs, crushed 808s and murky reverbs.", "Occult choirs|Crushed 808s|Murky reverbs" },
{ "Vaporwave", "vapor wave|mallsoft", true, { 35, 15, 80, 75, 5, 80, 5, 35, 95, 40, 20, 75, 50, 30 }, 85, 0.1f, "major", "halftime,rnb", "vaporwave",
  "Slowed, chopped 80s/90s smooth-music textures, heavy reverb, tape wobble and nostalgic chords.", "Slowed chopped chords|Tape wobble|Heavy reverb|Smooth jazz tones" },
{ "Lo-fi Hip Hop", "lofi hip hop|lo fi hip hop|chillhop|jazzhop", true, { 30, 15, 45, 85, 10, 50, 0, 10, 85, 45, 30, 85, 10, 5 }, 82, 0.45f, "dorian", "boom_bap", "lo-fi hip hop",
  "Dusty swung drums, jazzy electric piano, vinyl crackle and warm tape saturation.", "Dusty swung drums|Jazzy keys|Vinyl crackle|Tape warmth" },
{ "Boom Bap", "boombap|golden age hip hop", true, { 45, 30, 35, 65, 50, 30, 5, 10, 90, 60, 45, 60, 15, 5 }, 90, 0.3f, "minor", "boom_bap", "boom bap",
  "Punchy sampled-style kicks and cracking snares, loose swing, sampled soul and jazz.", "Punchy kicks|Cracking snares|Loose swing" },
{ "Alt R&B", "alternative r&b|r&b|rnb|pbr&b", true, { 50, 15, 70, 25, 15, 70, 5, 15, 45, 60, 30, 85, 30, 25 }, 90, 0.15f, "dorian", "rnb,halftime", "alternative R&B",
  "Lush chords, soft clicky drums, deep subs and heavily processed vocal layers.", "Lush chords|Soft drums|Deep subs|Processed vocals" },
{ "Pop", "electropop|synthpop|synth-pop", true, { 30, 20, 85, 10, 45, 45, 10, 15, 25, 60, 70, 80, 15, 40 }, 118, 0.0f, "major", "four_floor,halftime", "pop",
  "Polished punchy drums, bright synths, big chords and clean low end.", "Punchy drums|Bright synths|Big chords" },
{ "Hardstyle", "rawstyle|hard dance", true, { 60, 90, 95, 10, 90, 55, 40, 20, 15, 85, 95, 65, 25, 30 }, 150, 0.0f, "harmonic minor", "hardstyle,four_floor", "hardstyle",
  "Distorted pitched kicks with long tails, euphoric supersaw leads and huge reverbs.", "Distorted pitched kicks|Supersaw leads|Huge reverbs" },
{ "Gabber", "hardcore|gabba", true, { 75, 100, 90, 25, 100, 30, 60, 30, 40, 80, 100, 25, 40, 15 }, 180, 0.0f, "phrygian", "hardstyle,four_floor", "gabber, hardcore",
  "Massively distorted kicks at 170-200 BPM, hoover synths and relentless aggression.", "Distorted kicks|Hoover synths|Extreme tempo" },
{ "Footwork", "juke", true, { 45, 30, 80, 40, 55, 30, 15, 50, 35, 70, 95, 50, 55, 15 }, 160, 0.05f, "minor", "jersey,trap", "footwork",
  "160 BPM triplet-heavy kicks, chopped looping vocal snippets and rolling 808s.", "Triplet kicks|Looping vocal chops|Rolling 808s" },
{ "Jersey Club", "jersey", true, { 35, 35, 85, 20, 60, 30, 10, 35, 20, 70, 90, 55, 30, 45 }, 140, 0.0f, "minor", "jersey", "jersey club",
  "Bouncy triplet kick patterns, bed-squeak-style percussion and chopped vocals.", "Triplet kick bounce|Squeaky percussion|Chopped vocals" },
{ "Reggaeton", "dembow|perreo", true, { 35, 30, 80, 15, 50, 40, 5, 15, 25, 75, 75, 65, 20, 25 }, 95, 0.05f, "minor", "reggaeton", "reggaeton",
  "Dembow kick-snare pattern, deep bass, bright plucks and Latin percussion.", "Dembow pattern|Deep bass|Latin percussion" },
{ "Shoegaze", "dream pop|dreampop", true, { 40, 75, 35, 45, 40, 90, 15, 25, 70, 55, 50, 80, 40, 10 }, 115, 0.0f, "major", "breakbeat,halftime", "shoegaze, dream pop",
  "Walls of distorted, chorused texture, buried voices and huge reverse reverbs.", "Walls of distortion|Chorused textures|Reverse reverbs" },
{ "Post-Punk", "postpunk|coldwave", true, { 75, 45, 35, 45, 60, 55, 30, 10, 80, 45, 65, 50, 30, 0 }, 135, 0.0f, "minor", "breakbeat,four_floor", "post-punk",
  "Tight robotic drums, gated rooms, melodic bass and cold synth lines.", "Robotic drums|Gated rooms|Melodic bass" },
{ "Electroclash", "indie sleaze|blog house|electropunk", true, { 60, 80, 90, 45, 75, 35, 35, 35, 55, 60, 80, 50, 35, 25 }, 126, 0.05f, "minor", "electroclash,four_floor", "electroclash, blog house",
  "Distorted, compressed electro: crunchy drum machines, overdriven saw riffs and snotty vocals.", "Crunchy drum machines|Overdriven saws|Pumping compression" },
{ "Cloud Rap", "cloudrap", true, { 50, 25, 80, 40, 20, 90, 5, 20, 30, 70, 35, 80, 35, 35 }, 135, 0.0f, "major", "trap,halftime", "cloud rap",
  "Dreamy washed-out pads, soft 808s and floaty, reverb-heavy vocal textures.", "Washed-out pads|Soft 808s|Floaty vocals" },
{ "Rage", "rage beats|opium", true, { 65, 85, 95, 25, 85, 45, 30, 35, 10, 90, 90, 50, 40, 55 }, 160, 0.0f, "minor", "trap,hyperpop", "rage",
  "Distorted supersaw leads, massive clipped 808s and rapid hats.", "Supersaw leads|Clipped 808s|Rapid hats" },
{ "Trip Hop", "triphop|downtempo", true, { 70, 30, 50, 55, 30, 75, 20, 15, 70, 65, 30, 70, 35, 5 }, 85, 0.2f, "minor", "boom_bap,halftime", "trip hop",
  "Slow heavy breaks, dub bass, vinyl texture and cinematic strings.", "Slow heavy breaks|Dub bass|Cinematic strings" },
{ "Chillwave", "glo-fi", true, { 30, 25, 75, 55, 10, 80, 5, 15, 85, 45, 40, 80, 25, 20 }, 100, 0.05f, "major", "four_floor,halftime", "chillwave",
  "Sun-faded synths, sidechained pads, tape wobble and soft drum machines.", "Sun-faded synths|Tape wobble|Soft drum machines" },
{ "Future Bass", "futurebass", true, { 25, 40, 95, 10, 50, 55, 10, 40, 10, 65, 70, 85, 30, 65 }, 150, 0.0f, "major", "halftime,trap", "future bass",
  "Wobbling sidechained supersaw chords, vocal chops and bright snappy drums.", "Supersaw chords|Vocal chops|Snappy drums" },
{ "Deconstructed Club", "deconstructed", true, { 70, 70, 95, 20, 75, 55, 80, 70, 10, 70, 60, 30, 95, 40 }, 115, 0.0f, "phrygian", "reggaeton,breakbeat,halftime", "deconstructed club",
  "Shattered club rhythms, metallic hits, gunshot-like impacts and alien textures.", "Metallic hits|Impacts|Alien textures" },
{ "Grime", "", true, { 70, 50, 85, 30, 75, 30, 35, 30, 30, 75, 80, 40, 40, 15 }, 140, 0.0f, "minor", "halftime,garage", "grime",
  "Square-wave bass, eski-click percussion, sparse 140 BPM drums and icy synths.", "Square-wave bass|Eski clicks|Icy synths" },
{ "Noise", "harsh noise|power electronics", true, { 85, 100, 70, 50, 95, 40, 90, 60, 30, 70, 60, 5, 95, 5 }, 120, 0.0f, "phrygian", "industrial", "noise",
  "Walls of harsh distortion, feedback and abrasive metallic textures.", "Harsh distortion|Feedback|Abrasive textures" },
{ "Jazz", "jazz hop|nu jazz", true, { 30, 10, 20, 40, 20, 45, 0, 5, 80, 40, 45, 95, 30, 0 }, 100, 0.55f, "dorian", "boom_bap,breakbeat", "jazz",
  "Brushed drums, upright-like bass, extended piano chords and loose swing.", "Brushed drums|Extended chords|Loose swing" },
{ "Afrobeats", "afrobeat|amapiano", true, { 25, 15, 75, 15, 35, 40, 5, 10, 30, 65, 65, 75, 20, 20 }, 108, 0.3f, "major", "reggaeton,four_floor", "afrobeats, amapiano",
  "Syncopated percussion, log-drum-like basses, shakers and bright melodic plucks.", "Syncopated percussion|Log-drum bass|Shakers" },
{ "Metal", "heavy metal|metalcore|djent", true, { 80, 95, 25, 20, 95, 40, 50, 20, 40, 85, 90, 40, 30, 5 }, 150, 0.0f, "phrygian", "breakbeat,industrial", "metal",
  "Tight triggered kicks, cracking snares, distorted low-tuned riffs and huge cymbals.", "Triggered kicks|Cracking snares|Distorted low riffs" },
};

const std::vector<ArtistEntry>& artistDatabase() { return kDb; }

static std::vector<std::string> splitStr(const std::string& s, char sep) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (c == sep) { out.push_back(trim(cur)); cur.clear(); } else cur += c; }
    if (!trim(cur).empty()) out.push_back(trim(cur));
    return out;
}

SonicProfile profileFromEntry(const ArtistEntry& e) {
    SonicProfile p;
    for (int i = 0; i < kNumDesc; ++i) p.d[(size_t)i] = e.d[i] / 100.0f;
    p.bpm = e.bpm; p.swing = e.swing;
    Scale sc; if (scaleFromName(e.scale, sc)) p.scale = sc;
    for (auto& g : splitStr(e.grooves, ',')) { Groove gg; if (grooveFromId(g, gg)) p.grooves.push_back(gg); }
    if (p.grooves.empty()) p.grooves.push_back(Groove::FourFloor);
    p.displayName = e.name;
    p.summary = e.summary;
    p.genres = splitStr(e.genres, ',');
    p.signatures = splitStr(e.signatures, '|');
    return p;
}

// ---------------------------------------------------------------------------
std::string normaliseName(const std::string& in) {
    // Lowercase, transliterate common Latin-1 letters from UTF-8, keep [a-z0-9 ].
    std::string s; s.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char c = (unsigned char)in[i];
        if (c == 0xC3 && i + 1 < in.size()) {
            unsigned char d = (unsigned char)in[++i];
            const char* map = "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
            char m = (d >= 0xA0 && d <= 0xBF) ? map[d - 0xA0] : (d >= 0x80 && d <= 0x9F) ? (char)std::toupper((unsigned char)map[d - 0x80]) : ' ';
            s += (char)std::tolower((unsigned char)m);
            continue;
        }
        if (c == '$') { s += 's'; continue; }
        if (std::isalnum(c)) s += (char)std::tolower(c);
        else if (c == ' ' || c == '-' || c == '_' || c == '.' || c == ',' || c == '/' || c == '\'') s += ' ';
        else if (c == '&') s += " and ";
    }
    // collapse spaces
    std::string o; bool sp = false;
    for (char c : s) { if (c == ' ') { if (!sp && !o.empty()) o += ' '; sp = true; } else { o += c; sp = false; } }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    if (o.rfind("the ", 0) == 0) o = o.substr(4);
    return o;
}

static std::string squash(const std::string& s) { std::string o; for (char c : s) if (c != ' ') o += c; return o; }

static int levenshtein(const std::string& a, const std::string& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = (int)j;
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = (int)i;
        for (size_t j = 1; j <= b.size(); ++j)
            cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1) });
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

static std::vector<std::string> keysFor(const ArtistEntry& e) {
    std::vector<std::string> k = { normaliseName(e.name) };
    for (auto& a : splitStr(e.aliases, '|')) if (!a.empty()) k.push_back(normaliseName(a));
    return k;
}

namespace {
struct Match { const ArtistEntry* e = nullptr; float score = 0; };

Match bestMatch(const std::string& qNorm) {
    Match best;
    std::string qs = squash(qNorm);
    if (qs.empty()) return best;
    for (auto& e : kDb) {
        for (auto& k : keysFor(e)) {
            std::string ks = squash(k);
            if (ks.empty()) continue;
            float score = 0;
            if (ks == qs) score = 1.0f;
            else if (qs.size() >= 4 && ks.size() >= 4) {
                int dist = levenshtein(qs, ks);
                float sim = 1.0f - (float)dist / (float)std::max(qs.size(), ks.size());
                float thr = std::max(qs.size(), ks.size()) >= 10 ? 0.74f : 0.8f;
                if (sim >= thr) score = 0.6f + 0.35f * (sim - thr) / (1.0f - thr);
            }
            if (score > best.score) { best.e = &e; best.score = score; }
        }
    }
    return best;
}

// Find genre/artist names that occur as whole words inside a longer query.
std::vector<const ArtistEntry*> containedEntries(const std::string& qNorm) {
    std::vector<const ArtistEntry*> out;
    std::string padded = " " + qNorm + " ";
    std::vector<std::pair<size_t, const ArtistEntry*>> found;
    for (auto& e : kDb) {
        for (auto& k : keysFor(e)) {
            if (k.size() < 3) continue;
            if (padded.find(" " + k + " ") != std::string::npos) { found.push_back({ k.size(), &e }); break; }
        }
    }
    // Prefer longer, more specific matches; drop entries whose key is a substring of a longer match
    std::sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<std::string> taken;
    for (auto& [len, e] : found) {
        std::string k = normaliseName(e->name);
        bool sub = false;
        for (auto& t : taken) if (t.find(k) != std::string::npos) sub = true;
        if (!sub) { out.push_back(e); taken.push_back(k); }
    }
    return out;
}
}

static SonicProfile averageOf(const std::vector<const ArtistEntry*>& es) {
    SonicProfile p = profileFromEntry(*es[0]);
    for (size_t i = 1; i < es.size(); ++i) p = blendProfiles(p, profileFromEntry(*es[i]), 1.0f / (float)(i + 1));
    std::vector<std::string> names; for (auto* e : es) names.push_back(e->name);
    std::string dn; for (size_t i = 0; i < names.size(); ++i) dn += (i ? " × " : "") + names[i];
    p.displayName = dn;
    p.genres.clear(); p.signatures.clear();
    for (auto* e : es) {
        for (auto& g : splitStr(e->genres, ',')) if (std::find(p.genres.begin(), p.genres.end(), g) == p.genres.end()) p.genres.push_back(g);
        auto sig = splitStr(e->signatures, '|');
        for (size_t i = 0; i < std::min<size_t>(3, sig.size()); ++i) p.signatures.push_back(sig[i]);
    }
    std::string sum = "Blend of ";
    for (size_t i = 0; i < names.size(); ++i) sum += (i ? (i + 1 == names.size() ? " and " : ", ") : "") + names[i];
    p.summary = sum + ". " + es[0]->summary;
    return p;
}

AnalysisResult analyseQuery(const std::string& query) {
    AnalysisResult r;
    std::string q = trim(query);
    std::string qn = normaliseName(q);
    if (qn.empty()) {
        r.profile = neutralProfile(); r.method = "neutral";
        r.message = "No artist entered — using a neutral modern-production profile.";
        r.profile.query = q; r.profile.displayName = "Neutral"; r.profile.source = "neutral"; r.profile.confidence = 0;
        return r;
    }
    // 1) Whole query matches a known entry.
    Match m = bestMatch(qn);
    if (m.e && m.score >= 0.6f) {
        r.profile = profileFromEntry(*m.e);
        r.method = "offline-db";
        r.matched = { m.e->name };
        r.profile.confidence = m.score;
        r.message = m.score >= 0.99f ? std::string("Matched \"") + m.e->name + "\" in the offline sound database."
                                     : std::string("Interpreted \"") + q + "\" as \"" + m.e->name + "\" (fuzzy match).";
    } else {
        // 2) Blends: "x meets y", "x + y", or genre/descriptor phrases.
        std::string sep = qn;
        for (const char* conn : { " meets ", " vs ", " versus ", " x ", " and ", " with ", " plus ", " mixed with " }) {
            size_t pos; while ((pos = sep.find(conn)) != std::string::npos) sep.replace(pos, std::strlen(conn), "|");
        }
        std::vector<const ArtistEntry*> parts;
        for (auto& part : splitStr(sep, '|')) {
            Match pm = bestMatch(part);
            if (pm.e && pm.score >= 0.75f) { parts.push_back(pm.e); continue; }
            for (auto* e : containedEntries(part)) parts.push_back(e);
        }
        // de-dup
        std::vector<const ArtistEntry*> uniq;
        for (auto* e : parts) if (std::find(uniq.begin(), uniq.end(), e) == uniq.end()) uniq.push_back(e);
        if (!uniq.empty()) {
            r.profile = averageOf(uniq);
            r.method = "genre-blend";
            for (auto* e : uniq) r.matched.push_back(e->name);
            r.profile.confidence = 0.6f;
            r.message = "Built from " + std::to_string(uniq.size()) + " known reference" + (uniq.size() > 1 ? "s" : "") + " found in your query.";
        }
    }
    // 3) Descriptor words inside the query ("dark lo-fi trap") always apply.
    if (r.method.empty() || r.method == "genre-blend") {
        SonicProfile base = r.method.empty() ? neutralProfile() : r.profile;
        PromptResult pr = applyPrompt(base, q);
        if (pr.any()) {
            if (r.method.empty()) {
                r.method = "descriptor";
                r.profile = base;
                r.profile.displayName = q;
                r.profile.confidence = 0.45f;
                r.profile.summary = "Built from the descriptive words in your query.";
                r.message = "No artist recognised; built a profile from the descriptive words in your query.";
            } else {
                std::string dn = r.profile.displayName, sm = r.profile.summary; auto g = r.profile.genres; auto s = r.profile.signatures;
                r.profile = base; r.profile.displayName = dn; r.profile.summary = sm; r.profile.genres = g; r.profile.signatures = s;
                r.message += " Descriptive words were applied on top.";
            }
        }
    }
    // 4) Unknown: deterministic approximate profile, clearly flagged.
    if (r.method.empty()) {
        SonicProfile p = neutralProfile();
        Rng rng(hashString(qn));
        for (auto& v : p.d) v = clampf(v + (float)rng.gauss() * 0.18f, 0.0f, 1.0f);
        p.bpm = (float)rng.range(80, 150);
        p.swing = (float)rng.range(0.0, 0.3);
        p.scale = (Scale)rng.irange(0, (int)Scale::Count - 1);
        p.grooves = { (Groove)rng.irange(0, (int)Groove::Count - 1), Groove::HalfTime };
        p.displayName = q;
        p.confidence = 0.1f;
        p.summary = "Unknown to the offline database — this is an approximate, seeded profile. Enable online analysis for a researched profile, or shape the sliders.";
        r.profile = p;
        r.method = "heuristic";
        r.message = "\"" + q + "\" is not in the offline database. Generated an approximate profile (low confidence). Use Online Analysis or adjust the sliders.";
    }
    r.profile.query = q;
    r.profile.source = r.method;
    return r;
}

std::vector<std::string> suggestNames(const std::string& partial, int maxResults) {
    std::vector<std::pair<float, std::string>> scored;
    std::string qn = squash(normaliseName(partial));
    if (qn.empty()) return {};
    for (auto& e : kDb) {
        float best = 0;
        for (auto& k : keysFor(e)) {
            std::string ks = squash(k);
            if (ks.rfind(qn, 0) == 0) best = std::max(best, 1.0f + (float)qn.size() / (float)ks.size());
            else if (ks.find(qn) != std::string::npos) best = std::max(best, 0.8f);
            else if (qn.size() >= 3) {
                std::string pre = ks.substr(0, std::min(ks.size(), qn.size()));
                float sim = 1.0f - (float)levenshtein(qn, pre) / (float)qn.size();
                if (sim >= 0.7f) best = std::max(best, sim * 0.7f);
            }
        }
        if (best > 0) scored.push_back({ best, e.name });
    }
    std::sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<std::string> out;
    for (auto& s : scored) { if ((int)out.size() >= maxResults) break; out.push_back(s.second); }
    return out;
}

} // namespace sdna

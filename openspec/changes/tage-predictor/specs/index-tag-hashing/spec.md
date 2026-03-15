## ADDED Requirements

### Requirement: Index is 9 bits computed as PC XOR GHR_hash
The table index SHALL be 9 bits, computed as `PC[15:7] XOR GHR_hash`, where GHR_hash is a 9-bit value derived from specific GHR bits selected per table.

#### Scenario: Index uses correct PC bits
- **WHEN** computing the index for any table
- **THEN** the PC contribution SHALL be bits [15:7] (9 bits)

#### Scenario: Index XORs GHR_hash
- **WHEN** computing the index for table T
- **THEN** the final index = PC[15:7] XOR GHR_hash_T (9 bits)

### Requirement: Each table has a distinct GHR_hash definition
Each TAGE table SHALL use a distinct 9-bit GHR_hash computed by XOR-ing specified GHR bit positions. The GHR_hash definitions are:

- **T1**: {GHR[10], GHR[2], GHR[3], GHR[4], GHR[5], GHR[6], GHR[7], GHR[8], GHR[9]}
- **T2**: {GHR[8], GHR[9]^GHR[10], GHR[1]^GHR[11], GHR[2], GHR[3], GHR[4], GHR[5], GHR[6], GHR[7]}
- **T3**: {GHR[12], GHR[14], GHR[16], GHR[0], GHR[2], GHR[4], GHR[6], GHR[8], GHR[10]}
- **T4**: {GHR[11], GHR[13], GHR[15], GHR[17]^GHR[19], GHR[1]^GHR[21], GHR[3]^GHR[23], GHR[5]^GHR[25], GHR[7]^GHR[27], GHR[9]^GHR[29]}
- **T5**: {GHR[10]^GHR[30]^GHR[50], GHR[12]^GHR[32]^GHR[52], GHR[14]^GHR[34], GHR[16]^GHR[36]^GHR[38], GHR[18]^GHR[20]^GHR[40], GHR[2]^GHR[22]^GHR[42], GHR[4]^GHR[24]^GHR[44], GHR[6]^GHR[26]^GHR[46], GHR[8]^GHR[28]^GHR[48]}
- **T6**: {GHR[6]^GHR[28]^GHR[48]^GHR[68], GHR[8]^GHR[30]^GHR[50]^GHR[70], GHR[12]^GHR[32]^GHR[52]^GHR[72], GHR[14]^GHR[34]^GHR[54]^GHR[56], GHR[16]^GHR[36]^GHR[38]^GHR[58], GHR[18]^GHR[20]^GHR[40]^GHR[60], GHR[0]^GHR[22]^GHR[42]^GHR[62], GHR[2]^GHR[24]^GHR[44]^GHR[64], GHR[4]^GHR[26]^GHR[46]^GHR[66]}
- **T7**: {GHR[11]^GHR[61]^GHR[111]^GHR[161], GHR[16]^GHR[66]^GHR[116]^GHR[166], GHR[21]^GHR[71]^GHR[121]^GHR[171], GHR[26]^GHR[76]^GHR[126], GHR[31]^GHR[81]^GHR[131]^GHR[136], GHR[36]^GHR[86]^GHR[91]^GHR[141], GHR[41]^GHR[46]^GHR[96]^GHR[146], GHR[1]^GHR[51]^GHR[101]^GHR[151], GHR[6]^GHR[56]^GHR[106]^GHR[156]}
- **T8**: {GHR[15]^GHR[145]^GHR[275]^GHR[405], GHR[28]^GHR[158]^GHR[288]^GHR[418], GHR[41]^GHR[171]^GHR[301]^GHR[431], GHR[54]^GHR[184]^GHR[314]^GHR[444], GHR[67]^GHR[197]^GHR[327], GHR[80]^GHR[210]^GHR[340]^GHR[353], GHR[93]^GHR[223]^GHR[236]^GHR[366], GHR[106]^GHR[119]^GHR[249]^GHR[379], GHR[2]^GHR[132]^GHR[262]^GHR[392]}

Note: GHR bit index refers to the bit position within the flattened 456-bit GHR (3 bits per entry × entry index, MSB first within each entry).

#### Scenario: T1 GHR_hash computation
- **WHEN** computing the index for T1
- **THEN** GHR_hash = {GHR[10], GHR[2..9]} (9 bits as defined above)

#### Scenario: T8 uses deep GHR bits
- **WHEN** computing the index for T8
- **THEN** GHR_hash uses bits up to GHR[444], which requires GHR depth >= 152 entries × 3 bits

### Requirement: Tag is 13 bits derived from PC[15:7]
The tag for each TAGE table entry SHALL be 13 bits. The tag hashing base SHALL use PC[15:7], consistent with the index hashing. The 13-bit tag SHALL be derived from PC[15:3] (PC bits 15 down to 3).

#### Scenario: Tag width
- **WHEN** a tag is stored or compared for any table entry
- **THEN** the tag SHALL be exactly 13 bits wide

#### Scenario: Tag uses PC[15:7] as hashing base
- **WHEN** computing the tag for any TAGE table
- **THEN** the PC contribution to the tag SHALL be based on PC[15:7], consistent with the index hashing base

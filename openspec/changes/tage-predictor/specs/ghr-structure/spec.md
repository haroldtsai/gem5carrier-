## ADDED Requirements

### Requirement: GHR is only-taken with 3-bit entries
The GHR SHALL consist of 152 entries, each 3 bits wide, for a total of 456 bits. Only taken branches SHALL be written to the GHR. Not-taken branches SHALL NOT update the GHR.

#### Scenario: Taken branch updates GHR
- **WHEN** a branch is predicted and its direction is taken
- **THEN** a new 3-bit entry is pushed into the GHR head, computed as branch_pc[7:5] XOR branch_pc[4:2] XOR target_pc[7:5] XOR target_pc[4:2]

#### Scenario: Not-taken branch does not update GHR
- **WHEN** a branch is predicted and its direction is not-taken
- **THEN** the GHR SHALL remain unchanged

### Requirement: GHR entry value computation
Each 3-bit GHR entry SHALL be computed as: `branch_pc[7:5] ^ branch_pc[4:2] ^ target_pc[7:5] ^ target_pc[4:2]`

#### Scenario: Entry value uses correct bits
- **WHEN** a taken branch at PC=0xABC with target=0x123 is recorded
- **THEN** the 3-bit entry = PC[7:5] XOR PC[4:2] XOR target[7:5] XOR target[4:2]

### Requirement: GHR is updated speculatively at prediction time
The GHR SHALL be updated at prediction time (speculative update), not at commit time.

#### Scenario: Speculative GHR update on prediction
- **WHEN** a taken branch is predicted (before commit)
- **THEN** the GHR is updated immediately with the new 3-bit entry

### Requirement: GHR checkpoint and restore on misprediction
A checkpoint of the GHR state SHALL be saved at each prediction. On misprediction, the GHR SHALL be restored to the checkpointed state.

#### Scenario: GHR restore on misprediction
- **WHEN** a misprediction is detected for a branch
- **THEN** the GHR SHALL be restored to the snapshot taken at the time that branch was predicted

#### Scenario: Checkpoint saved at prediction
- **WHEN** any branch is predicted
- **THEN** a GHR checkpoint (head pointer + modified entry) SHALL be saved and associated with the branch sequence number

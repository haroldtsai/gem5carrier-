## ADDED Requirements

### Requirement: Allocation only occurs on misprediction
A new TAGE entry SHALL only be allocated when a branch is mispredicted. Correct predictions SHALL NOT trigger allocation.

#### Scenario: Misprediction triggers allocation attempt
- **WHEN** a branch is mispredicted
- **THEN** the predictor SHALL attempt to allocate a new entry in a table longer than the provider

#### Scenario: Correct prediction does not allocate
- **WHEN** a branch is correctly predicted
- **THEN** no new entry SHALL be allocated in any TAGE table

### Requirement: Victim selection requires useful counter == 0
When allocating, the victim SHALL be selected from tables with longer history than the provider. A candidate entry is eligible only if its useful counter == 0.

#### Scenario: First eligible useful==0 entry is selected
- **WHEN** a misprediction occurs and provider is T3
- **THEN** the allocator SHALL search T4..T8 (in order) for an entry with useful==0 at the branch's index

#### Scenario: Multiple eligible tables — allocate in one
- **WHEN** T5 and T7 both have useful==0 entries at the branch's index
- **THEN** one of them SHALL be selected as the allocation target (implementation may choose any)

### Requirement: Newly allocated entry initial values
A newly allocated TAGE entry SHALL be initialized as:
- useful counter = 1
- pred counter = 4 (binary 100) if branch was taken, or 3 (binary 011) if branch was not-taken

#### Scenario: Taken misprediction sets pred counter to 4
- **WHEN** a taken branch is mispredicted and a new entry is allocated
- **THEN** the new entry's pred counter SHALL be 4 (100)

#### Scenario: Not-taken misprediction sets pred counter to 3
- **WHEN** a not-taken branch is mispredicted and a new entry is allocated
- **THEN** the new entry's pred counter SHALL be 3 (011)

### Requirement: Fallback useful decrement when no eligible victim found
If no table longer than the provider has an entry with useful==0 at the branch's index, the predictor SHALL decrement the useful counter of the corresponding entry in every such longer table.

#### Scenario: All longer tables have useful > 0, decrement all
- **WHEN** a misprediction occurs, provider is T2, and T3..T8 all have useful > 0 at the branch's index
- **THEN** the useful counter of each corresponding entry in T3..T8 SHALL be decremented (saturate at 0)

### Requirement: No periodic useful counter aging
The predictor SHALL NOT implement any periodic or time-based reset of useful counters. Useful counters SHALL only change via allocation (set to 1), fallback decrement, or explicit update.

#### Scenario: Useful counter not reset periodically
- **WHEN** a large number of branches have been processed
- **THEN** useful counters SHALL NOT be reset due to a timer or instruction count threshold

# Historical pre-refactor evidence

These files preserve the b-19/l-20/g-21 firmware's raw startup, benchmark,
memory, output validation and live snapshots before the shared-node refactor.
`record_b19.yml` is the original benchmark record, renamed so active benchmark
discovery does not treat this historical archive as a current configuration.
References inside the archived reports describe their original project/model
locations at the time of validation. Current files one directory above identify
the new firmware. The model/PTE and Vela estimate are unchanged; the archived
record retains their hashes. Its final parent snapshot reached 5,720 frames with
full-frame max 154.99975 ms; that later inspection is recorded in CHECKPOINT.md.

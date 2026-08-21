#include "replay/ReplayCompletion.hpp"

namespace realmz::replay {

ReplayCompletedResult complete_replay(
    ReplayRuntime& runtime,
    const ReplayCompletionOperations& operations) {
  if (!operations.save_slot || !operations.verify_output_slot ||
      !operations.publish_result) {
    throw ReplayCompletionError(
        "replay completion operations are incomplete");
  }
  if (operations.process_id == 0U) {
    throw ReplayCompletionError("replay completion process id is zero");
  }
  if (runtime.planned_action_count() != runtime.config().actions().size() ||
      runtime.settled_action_count() != runtime.planned_action_count()) {
    throw ReplayCompletionError(
        "replay runtime action counts do not match the child config");
  }

  const Sha256Digest state_digest = runtime.finalize_state_trace();
  if (!operations.save_slot(runtime.output_slot())) {
    throw ReplayCompletionError("legacy replay output save failed");
  }
  VerifiedReplayOutput output = operations.verify_output_slot(
      runtime.user_data_root(), runtime.output_slot());

  ReplayCompletedResult result{
      .process_id = operations.process_id,
      .engine_identity = operations.engine_identity,
      .settled_action_count = runtime.settled_action_count(),
      .state_sha256 = state_digest,
      .save_tree_sha256 = output.tree_sha256,
      .rng_draw_count = runtime.rng_draw_count(),
  };
  operations.publish_result(runtime.config(), result);
  return result;
}

} // namespace realmz::replay

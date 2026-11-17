# Artist following

Artist profiles and the desktop player dock expose a shared follow action. Android provides the same action in artist profiles, the mini player and the expanded player. The compact control uses a person with a plus or check mark; its accessible name identifies the artist.

Subscriptions belong to the signed-in SoundCloud account. The client lazily loads `/me/followings` with pagination before enabling mutations, then uses `PUT` or `DELETE /me/followings/{user_urn}`. No subscription data is stored in the repository or local preferences.

Controls share account state and per-artist pending state. Requests for the same artist are serialized. A failed mutation preserves the previous subscription state and reports the network error. Failed initial synchronization can be retried from the control. Self-follow controls are hidden and rejected by the client. Logout clears subscription state. Successful changes update available profile counts and invalidate cached feed and home recommendations for their next load.

The `followingsPaginateAndMutationsAreSerialized` transport test covers pagination, duplicate actions, HTTP failures, follow/unfollow methods, self-follow rejection and account state cleanup.

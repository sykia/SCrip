# Collection rendering

TrackModel updates existing rows through dataChanged when switching between cached
collections, including when track identifiers differ. Only added or removed rows
change the model structure. Clearing the collection resets the model to release
pooled delegates. Track selection is reset separately by the QML collection context.

Home shelves, mix rows, queues and playlist rows use their array length as the QML
model and bind each delegate's data to its index. Publishing catalog state (for
example, a pending or confirmed like) therefore updates bindings without replacing
unchanged delegates and reloading their artwork. Do not bind these views directly
to QVariantList snapshots: replacing an array makes Qt rebuild its delegates.

Regression coverage in tst_soundcloud.cpp checks delegate identity across collection
switches and home-card identity and like state after both liking and unliking.

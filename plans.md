# Plans for Riff Man

Riff Man will essentially be Spotify without all the cruft that I hate.

* Local-first, cloud-second.
* Simple, clean user interface.
* No unexpected page updates.
* No DRM. You control your own library, at the expense of having to procure the songs yourself.
* Background playback (hopefully this isn't an issue on mobile)
* Desktop and mobile compatible
* Concepts of releases (e.g. albums/eps/singles) and playlists under the umbrella term "collections"
* Collections collect audio tracks into whatever the user wants.
* Collections may have metadata (e.g. artist, release date, last update date, stuff like that)
* Collections may have their own renderers
    * A release is functionally a playlist, but not conceptually.
    * Having different renderers lets you make this distinction without needing to make a whole new class of collections.
* SQLite-powered collections.
    * Songs can have their own 64-bit integer ID
    * Collections can have their own 64-bit integer ID as well
    * Bam! You now have more potential playlists than you will ever need, ever!
* Support for playing back many common audio formats (raylib supports a few by default)
* tobideru... noboriryuu...

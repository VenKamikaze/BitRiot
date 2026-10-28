// RumbleSink.h
//
// Where the simulation sends "this player's controller should rumble". The
// simulation must not touch a device, so it calls this interface and the game
// installs the real thing (SDL haptics, in MickSDLUtil.h) at start-up. With none
// installed the call is dropped, which is what a headless run, a test or the web
// build wants. Brief 04b replaces the call from the simulation with a per-tick
// record that the local layer acts on; until then this keeps the SDL type and the
// float strength out of bitriot_sim.
#ifndef RUMBLESINK_H_
#define RUMBLESINK_H_

class RumbleSink
{
  public:
    virtual ~RumbleSink() {}

    // controllerId is the device id the input layer attached to the player
    // (PlayerCharacterEntity::attachedController). strengthPermille runs from 0 to
    // 1000 (an integer, so the simulation needs no float for it).
    virtual void rumble(int controllerId, int strengthPermille, int durationMs) = 0;

    // The installed sink, or a built-in one that drops every request. Never null.
    static RumbleSink* getInstance();

    // Installs `sink` (not owned; it must outlive its use), or removes it with nullptr.
    static void setInstance(RumbleSink* sink);
};

#endif /* RUMBLESINK_H_ */

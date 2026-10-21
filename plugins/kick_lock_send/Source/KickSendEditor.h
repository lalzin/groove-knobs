#pragma once

#include "KickSendProcessor.h"

namespace kls
{
/** Kick Lock Send: the suite's one-knob layout (SENSITIVITY), Phaze, and the
    link's state at the bottom (sending to a Kick Lock or waiting for one). */
class KickSendEditor final : public gk::OneKnobEditor
{
public:
    explicit KickSendEditor (KickSendProcessor& p);
    ~KickSendEditor() override;
    static gk::Palette makePalette();
    /** Screenshots: the link's state read now (the screen's refresh does it otherwise). */
    void refreshNow() { editorTick (0.0); }

protected:
    void editorTick (double dt) override;

private:
    class LinkStatus;
    std::unique_ptr<LinkStatus> status;
};
} // namespace kls

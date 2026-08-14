#include "PresentationHost.hpp"

#include <stdexcept>

namespace realmz::presentation {

namespace {

void validate_mode(PresentationMode mode) {
  switch (mode) {
    case PresentationMode::classic:
    case PresentationMode::remastered:
      return;
  }
  throw std::invalid_argument("invalid presentation mode");
}

} // namespace

PresentationMode ClassicRenderer::mode() const noexcept {
  return PresentationMode::classic;
}

PresentationRenderPath ClassicRenderer::present(
    ClassicFrameTarget& target) const {
  target.present_classic_frame();
  return PresentationRenderPath::classic;
}

PresentationMode RemasterRenderer::mode() const noexcept {
  return PresentationMode::remastered;
}

PresentationRenderPath RemasterRenderer::present(
    ClassicFrameTarget& target) const {
  target.present_remastered_frame();
  return PresentationRenderPath::remastered_shell;
}

PresentationHost::PresentationHost(PresentationMode mode) {
  this->set_mode(mode);
}

PresentationMode PresentationHost::mode() const noexcept {
  return this->mode_;
}

void PresentationHost::set_mode(PresentationMode mode) {
  validate_mode(mode);
  this->mode_ = mode;
}

const PresentationRenderer& PresentationHost::active_renderer() const noexcept {
  switch (this->mode_) {
    case PresentationMode::classic:
      return this->classic_renderer_;
    case PresentationMode::remastered:
      return this->remaster_renderer_;
  }
  // set_mode rejects invalid values, so this is unreachable without memory
  // corruption. Keep a deterministic fallback for noexcept callers.
  return this->classic_renderer_;
}

PresentationRenderPath PresentationHost::present(
    ClassicFrameTarget& target) const {
  return this->active_renderer().present(target);
}

} // namespace realmz::presentation

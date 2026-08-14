#pragma once

#include "PresentationMode.hpp"

namespace realmz::presentation {

// The presentation layer may submit an already-composited Classic framebuffer
// but cannot read or mutate engine state through this interface.
class ClassicFrameTarget {
public:
  virtual ~ClassicFrameTarget() = default;
  virtual void present_classic_frame() = 0;
  virtual void present_remastered_frame() = 0;
};

enum class PresentationRenderPath {
  classic,
  remastered_shell,
};

class PresentationRenderer {
public:
  virtual ~PresentationRenderer() = default;

  [[nodiscard]] virtual PresentationMode mode() const noexcept = 0;
  [[nodiscard]] virtual PresentationRenderPath present(
      ClassicFrameTarget& target) const = 0;
};

// Owns the pixel-identical legacy framebuffer submission path.
class ClassicRenderer final : public PresentationRenderer {
public:
  [[nodiscard]] PresentationMode mode() const noexcept override;
  [[nodiscard]] PresentationRenderPath present(
      ClassicFrameTarget& target) const override;
};

// Owns the live responsive-shell route. The target decides whether its current
// legacy screen is embedded intact or is eligible for semantic replacement.
class RemasterRenderer final : public PresentationRenderer {
public:
  [[nodiscard]] PresentationMode mode() const noexcept override;
  [[nodiscard]] PresentationRenderPath present(
      ClassicFrameTarget& target) const override;

};

// Single presentation-mode owner used by the SDL host. All completed frames
// pass through active_renderer(), including transient direct-to-screen draws.
class PresentationHost {
public:
  explicit PresentationHost(
      PresentationMode mode = PresentationMode::classic);

  [[nodiscard]] PresentationMode mode() const noexcept;
  void set_mode(PresentationMode mode);

  [[nodiscard]] const PresentationRenderer& active_renderer() const noexcept;
  [[nodiscard]] PresentationRenderPath present(
      ClassicFrameTarget& target) const;

private:
  PresentationMode mode_ = PresentationMode::classic;
  ClassicRenderer classic_renderer_;
  RemasterRenderer remaster_renderer_;
};

} // namespace realmz::presentation

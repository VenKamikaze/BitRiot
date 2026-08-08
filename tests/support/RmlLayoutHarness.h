// RmlLayoutHarness.h
//
// Loads the game's real .rml/.rcss menu documents and computes their layout, so
// tests can assert on geometry - where elements actually land on screen - with
// no GPU, no window and no screenshots.
//
// RmlUi separates layout from rasterisation: Context::Update() resolves the
// full box model, and only Context::Render() touches the RenderInterface. So a
// render interface that does nothing is enough to get real, trustworthy
// positions and sizes out of the library.
//
// This is what makes "are the scoreboard columns aligned?" an assertable
// question rather than something needing eyes on a screenshot. Note the limit:
// it verifies geometry, not aesthetics. Column x-offsets matching to within a
// pixel is a fact this can establish; "the scoreboard looks good" is not.

#ifndef BITRIOT_TESTS_RMLLAYOUTHARNESS_H_
#define BITRIOT_TESTS_RMLLAYOUTHARNESS_H_

#include <gtest/gtest.h>

#include <RmlUi/Core.h>

#include <string>
#include <vector>

namespace bitriot_test
{

// A render interface that draws nothing. Every texture request succeeds with a
// dummy handle so that decorators referencing images (common.rcss uses
// `decorator: image(...)` on .background) cannot fail the document load.
class NullRenderInterface : public Rml::RenderInterface
{
  public:
    void RenderGeometry(Rml::Vertex* vertices, int num_vertices, int* indices, int num_indices,
                        Rml::TextureHandle texture, const Rml::Vector2f& translation) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(int x, int y, int width, int height) override;
    bool LoadTexture(Rml::TextureHandle& texture_handle, Rml::Vector2i& texture_dimensions,
                     const Rml::String& source) override;
    bool GenerateTexture(Rml::TextureHandle& texture_handle, const Rml::byte* source,
                         const Rml::Vector2i& source_dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture_handle) override;
};

// One-time RmlUi initialisation: file interface rooted at assets/menu/, the
// null renderer, the SDL2 system interface and the game's font. Registered from
// test_main.cpp alongside HeadlessEnvironment.
class RmlEnvironment : public ::testing::Environment
{
  public:
    void SetUp() override;
    void TearDown() override;

    // True if RmlUi came up. If this is false the layout tests must fail
    // loudly rather than silently skipping - a broken layout harness is a
    // reportable outcome, not a reason to weaken a claim.
    static bool available();
    static const std::string& initError();
};

// Geometry of one laid-out element, in document coordinates.
struct CellBox
{
  std::string text;
  float left = 0.0f;
  float top = 0.0f;
  float width = 0.0f;
  float height = 0.0f;

  float right() const { return left + width; }
  float centreX() const { return left + (width / 2.0f); }
};

// Fixture for tests that need a laid-out document. Creates and destroys a
// context per test so documents never leak between them.
class RmlLayoutTest : public ::testing::Test
{
  protected:
    void SetUp() override;
    void TearDown() override;

    // Loads a document from assets/menu/ and computes its layout. Returns
    // nullptr on failure. Call after any data model the document binds to has
    // been created on context().
    Rml::ElementDocument* loadDocument(const std::string& fileName);

    Rml::Context* context() { return m_context; }

    // Recomputes layout. Called by loadDocument; call again after mutating the
    // document or its data model.
    void updateLayout();

    // Depth-first search for the first descendant carrying the given class.
    Rml::Element* findByClass(Rml::Element* root, const std::string& className);

    // All descendants carrying the given class, in document order.
    std::vector<Rml::Element*> findAllByClass(Rml::Element* root, const std::string& className);

    // Immediate children that actually occupy space. Elements hidden by a false
    // `data-if` collapse to zero width, so this is what "the cells you can see"
    // means.
    std::vector<Rml::Element*> visibleChildren(Rml::Element* parent);

    static CellBox boxOf(Rml::Element* element);

    // Window size the context is created at. Matches the game's default
    // 17x13 map plus the InfoPanel, so layout matches what a player sees.
    static constexpr int CONTEXT_WIDTH = 704;
    static constexpr int CONTEXT_HEIGHT = 416;

  private:
    Rml::Context* m_context = nullptr;
    std::streambuf* m_savedCout = nullptr;
};

} // namespace bitriot_test

#endif // BITRIOT_TESTS_RMLLAYOUTHARNESS_H_

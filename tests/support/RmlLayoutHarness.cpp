#include "RmlLayoutHarness.h"

#include <cstdlib>
#include <iostream>

#include "RmlUI/glue/ShellFileInterface.h"
#include "RmlUI/glue/SystemInterfaceSDL2.h"

namespace bitriot_test
{

namespace
{
bool g_available = false;
std::string g_initError;
NullRenderInterface* g_renderInterface = nullptr;
RmlUiSDL2SystemInterface* g_systemInterface = nullptr;
ShellFileInterface* g_fileInterface = nullptr;

// Any non-zero handle will do; nothing ever dereferences it.
const Rml::TextureHandle DUMMY_TEXTURE = 1;
} // namespace

// ---------------------------------------------------------------------------
// NullRenderInterface
// ---------------------------------------------------------------------------

void NullRenderInterface::RenderGeometry(Rml::Vertex*, int, int*, int, Rml::TextureHandle,
                                         const Rml::Vector2f&)
{
}

void NullRenderInterface::EnableScissorRegion(bool) {}

void NullRenderInterface::SetScissorRegion(int, int, int, int) {}

bool NullRenderInterface::LoadTexture(Rml::TextureHandle& texture_handle,
                                      Rml::Vector2i& texture_dimensions, const Rml::String&)
{
  // Succeed unconditionally. The dimensions only matter to decorators sizing
  // themselves; nothing the layout tests assert on depends on them.
  texture_handle = DUMMY_TEXTURE;
  texture_dimensions = Rml::Vector2i(1, 1);
  return true;
}

bool NullRenderInterface::GenerateTexture(Rml::TextureHandle& texture_handle, const Rml::byte*,
                                          const Rml::Vector2i&)
{
  texture_handle = DUMMY_TEXTURE;
  return true;
}

void NullRenderInterface::ReleaseTexture(Rml::TextureHandle) {}

// ---------------------------------------------------------------------------
// RmlEnvironment
// ---------------------------------------------------------------------------

void RmlEnvironment::SetUp()
{
  // Paths are resolved relative to the working directory (the repository root),
  // exactly as MenuSDLRenderer::initGui does it.
  g_fileInterface = new ShellFileInterface("assets/menu/");
  g_renderInterface = new NullRenderInterface();
  g_systemInterface = new RmlUiSDL2SystemInterface();

  Rml::SetFileInterface(g_fileInterface);
  Rml::SetRenderInterface(g_renderInterface);
  Rml::SetSystemInterface(g_systemInterface);

  if (!Rml::Initialise())
  {
    g_initError = "Rml::Initialise() returned false";
    return;
  }

  if (!Rml::LoadFontFace("assets/fonts/FreeMono.ttf"))
  {
    g_initError = "failed to load assets/fonts/FreeMono.ttf";
    Rml::Shutdown();
    return;
  }

  g_available = true;
}

void RmlEnvironment::TearDown()
{
  if (g_available)
  {
    Rml::Shutdown();
    g_available = false;
  }
}

bool RmlEnvironment::available()
{
  return g_available;
}

const std::string& RmlEnvironment::initError()
{
  return g_initError;
}

// ---------------------------------------------------------------------------
// RmlLayoutTest
// ---------------------------------------------------------------------------

void RmlLayoutTest::SetUp()
{
  ASSERT_TRUE(RmlEnvironment::available())
      << "RmlUi layout harness failed to initialise: " << RmlEnvironment::initError();

  if (std::getenv("BITRIOT_TEST_VERBOSE") == nullptr)
  {
    m_savedCout = std::cout.rdbuf(nullptr);
  }

  m_context = Rml::CreateContext("layout-test", Rml::Vector2i(CONTEXT_WIDTH, CONTEXT_HEIGHT));
  ASSERT_NE(m_context, nullptr) << "could not create an RmlUi context";
}

void RmlLayoutTest::TearDown()
{
  if (m_context != nullptr)
  {
    Rml::RemoveContext(m_context->GetName());
    m_context = nullptr;
  }

  if (m_savedCout != nullptr)
  {
    std::cout.rdbuf(m_savedCout);
    m_savedCout = nullptr;
  }
}

Rml::ElementDocument* RmlLayoutTest::loadDocument(const std::string& fileName)
{
  Rml::ElementDocument* document = m_context->LoadDocument(fileName);
  if (document == nullptr)
  {
    return nullptr;
  }
  document->Show();
  updateLayout();
  return document;
}

void RmlLayoutTest::updateLayout()
{
  // Twice: the first pass flushes data-binding changes into the DOM, the second
  // lays out the result. Cheap, and it removes a class of ordering surprise.
  m_context->Update();
  m_context->Update();
}

Rml::Element* RmlLayoutTest::findByClass(Rml::Element* root, const std::string& className)
{
  if (root == nullptr)
  {
    return nullptr;
  }
  if (root->IsClassSet(className))
  {
    return root;
  }
  for (int i = 0; i < root->GetNumChildren(); i++)
  {
    if (Rml::Element* found = findByClass(root->GetChild(i), className))
    {
      return found;
    }
  }
  return nullptr;
}

std::vector<Rml::Element*> RmlLayoutTest::findAllByClass(Rml::Element* root,
                                                         const std::string& className)
{
  std::vector<Rml::Element*> found;
  if (root == nullptr)
  {
    return found;
  }
  if (root->IsClassSet(className))
  {
    found.push_back(root);
  }
  for (int i = 0; i < root->GetNumChildren(); i++)
  {
    std::vector<Rml::Element*> childFound = findAllByClass(root->GetChild(i), className);
    found.insert(found.end(), childFound.begin(), childFound.end());
  }
  return found;
}

std::vector<Rml::Element*> RmlLayoutTest::visibleChildren(Rml::Element* parent)
{
  std::vector<Rml::Element*> visible;
  if (parent == nullptr)
  {
    return visible;
  }
  for (int i = 0; i < parent->GetNumChildren(); i++)
  {
    Rml::Element* child = parent->GetChild(i);
    if (child->GetBox().GetSize(Rml::Box::BORDER).x > 0.0f)
    {
      visible.push_back(child);
    }
  }
  return visible;
}

CellBox RmlLayoutTest::boxOf(Rml::Element* element)
{
  CellBox box;
  if (element == nullptr)
  {
    return box;
  }
  const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::Box::BORDER);
  const Rml::Vector2f size = element->GetBox().GetSize(Rml::Box::BORDER);
  box.text = element->GetInnerRML();
  box.left = offset.x;
  box.top = offset.y;
  box.width = size.x;
  box.height = size.y;
  return box;
}

} // namespace bitriot_test

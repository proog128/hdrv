#include <view/ImageRenderer.hpp>

char const* vertexSource = R"(
attribute highp vec2 vertices;
varying highp vec2 coords;

void main()
{
  gl_Position = vec4(vertices, 0, 1);
  coords = vertices * 0.5 + 0.5;
})";

char const* fragmentSource = R"(
uniform sampler2D tex;
uniform vec2 position;
uniform vec2 scale;
uniform float gamma;
varying highp vec2 coords;

void main()
{
  vec4 texel = texture2D(tex, (coords - position) / scale);
  vec3 color = pow(texel.xyz, vec3(gamma));
  gl_FragColor = vec4(color, texel.w);
})";

float const vertexData[] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};

std::unique_ptr<QOpenGLShaderProgram> createProgram()
{
  auto program = std::make_unique<QOpenGLShaderProgram>();
  program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexSource);
  program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSource);
  program->bindAttributeLocation("vertices", 0);
  program->link();
  return program;
}

namespace hdrv {

QOpenGLTexture::TextureFormat format(Image const& image)
{
  return image.channels() == 3 ? QOpenGLTexture::RGBFormat : QOpenGLTexture::RGBAFormat;
}

QOpenGLTexture::PixelFormat pixelFormat(Image const& image)
{
  return image.channels() == 3 ? QOpenGLTexture::RGB : QOpenGLTexture::RGBA;
}

std::unique_ptr<QOpenGLTexture> createTexture(Image const& image)
{
  auto texture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
  texture->setSize(image.width(), image.height());
  texture->setFormat(format(image));
  texture->allocateStorage(pixelFormat(image), QOpenGLTexture::PixelType::Float32);
  texture->setData(pixelFormat(image), QOpenGLTexture::PixelType::Float32, image.data());
  texture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
  texture->setMagnificationFilter(QOpenGLTexture::Linear);
  texture->setWrapMode(QOpenGLTexture::ClampToBorder);
  texture->generateMipMaps();
  return texture;
}

QVector2D texturePosition(QVector2D regionSize, QVector2D imageSize, QVector2D imagePosition)
{
  auto offset = (regionSize - imageSize) / 2.0f;
  return (offset + QVector2D(-imagePosition.x(), imagePosition.y())) / regionSize;
}

QVector2D textureScale(QVector2D regionSize, QVector2D imageSize)
{
  return imageSize / regionSize;
}

void ImageRenderer::setCurrent(std::shared_ptr<Image> const& image)
{
  current_ = image;
}

void ImageRenderer::updateImages(std::vector<ImageDocument *> const& images)
{
  // Erase textures for images that no longer exist
  for (auto iter = textures_.begin(); iter != textures_.end(); ) {
    auto matchImage = [iter](ImageDocument * doc) { return doc->image() == iter->first; };
    if (std::find_if(images.begin(), images.end(), matchImage) == images.end()) {
      textures_.erase(iter++);
    } else {
      ++iter;
    }
  }
  // Create textures for new images
  for (auto doc : images) {
    auto & tex = textures_[doc->image()];
    if (!tex) {
      tex = createTexture(*doc->image());
    }
  }
}

void ImageRenderer::paint()
{
  if (!program_) {
    initializeOpenGLFunctions();
    program_ = createProgram();
  }
  
  program_->bind();
  program_->enableAttributeArray(0);
  program_->setAttributeArray(0, GL_FLOAT, vertexData, 2);

  auto const& region = renderRegion_;
  auto const& image = *current_;
  auto & texture = *textures_[current_];
  QVector2D regionSize(float(region.size.width()), float(region.size.height()));
  QVector2D imageSize(float(image.width()) * settings_.scale, float(image.height()) * settings_.scale);

  texture.setBorderColor(clearColor_);
  texture.bind(0);
  program_->setUniformValue("tex", 0);
  program_->setUniformValue("position", texturePosition(regionSize, imageSize, settings_.position));
  program_->setUniformValue("scale", textureScale(regionSize, imageSize));
  program_->setUniformValue("gamma", 1.0f / settings_.gamma);

  glViewport(region.offset.x(), region.offset.y(), region.size.width(), region.size.height());

  glDisable(GL_DEPTH_TEST);
  glClearColor(clearColor_.red(), clearColor_.green(), clearColor_.blue(), 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  texture.release(0);
  program_->disableAttributeArray(0);
  program_->release();
}

}
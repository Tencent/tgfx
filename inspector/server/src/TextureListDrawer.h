#ifndef TEXTURELISTDRAWER_H
#define TEXTURELISTDRAWER_H

#include <QStringList>
#include <QVector>
#include <memory>
#include "AppHost.h"
#include "tgfx/gpu/opengl/qt/QGLWindow.h"
namespace inspector {
class TextureListDrawer : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(QString imageLabel WRITE setImageLabel)
 public:
  explicit TextureListDrawer(QQuickItem* parent = nullptr);
  ~TextureListDrawer() = default;

  Q_SIGNAL void selectedImage(std::shared_ptr<tgfx::Image> image);

  void updateImageData(std::initializer_list<std::string>& testImageSources);
  void setImageLabel(const QString& label);

 protected:
  QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;
  void mousePressEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

 private:
  void updateLayout();
  void draw();
  int itemAtPosition(float y) const;

  std::vector<tgfx::Rect> squareRects;
  std::vector<std::shared_ptr<tgfx::Image>> images;
  bool layoutDirty = true;
  float scrollOffset;

  std::shared_ptr<tgfx::QGLWindow> tgfxWindow = nullptr;
  std::shared_ptr<AppHost> appHost = nullptr;
};
}  // namespace inspector

#endif  // TEXTURELISTDRAWER_H
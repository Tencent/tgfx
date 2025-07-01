#include "TextureListDrawer.h"
#include <QRandomGenerator>
#include <iostream>
#include <qdir.h>

#include "Draw.h"
#include <QSGImageNode>
namespace inspector {
    static tgfx::Rect calcInerRect(const tgfx::Rect &rect, float aspectRatio) {
        auto w = rect.width();
        auto h = rect.height();
        const auto paddingRatio = 0.05f;
        const auto innerScaleRatio = 1-2*paddingRatio;
        if (w<=h*aspectRatio) {//外部矩形更扁
            auto innerWidth = w * innerScaleRatio;
            auto innerHeight = innerWidth / aspectRatio;
            auto x = paddingRatio * w;
            auto y = (h - innerHeight) / 2;
            return tgfx::Rect::MakeXYWH(x+rect.x(), y+rect.y(), innerWidth, innerHeight);
        } else {//外部矩形更瘦
            auto innerHeight = h * innerScaleRatio;
            auto innerWidth = innerHeight * aspectRatio;
            auto x = (w - innerWidth) / 2;
            auto y = paddingRatio * h;
            return tgfx::Rect::MakeXYWH(x+rect.x(), y+rect.y(), innerWidth, innerHeight);
        }
    }

    TextureListDrawer::TextureListDrawer(QQuickItem *parent)
        : QQuickItem(parent), appHost(AppHostSingleton::GetInstance())
    {
        setFlag(ItemHasContents, true);
        setAcceptHoverEvents(true);
        setAcceptedMouseButtons(Qt::AllButtons);
    }

    void TextureListDrawer::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
    {
        QQuickItem::geometryChange(newGeometry, oldGeometry);
        layoutDirty = true;
        update();
        if (tgfxWindow) {
            tgfxWindow->invalidSize();
        }
    }

    void TextureListDrawer::updateLayout()
    {
        if (!layoutDirty) {
            return;
        }
        squareRects.clear();
        auto y = 0.f;
        const auto squareSize = static_cast<float>(width());
        for (size_t i=0;i<images.size();i++) {
            squareRects.push_back(tgfx::Rect::MakeXYWH(0.f, y, squareSize, squareSize));
            y += squareSize;
        }

        setImplicitHeight(y);
        layoutDirty = false;
    }

    int TextureListDrawer::itemAtPosition(float y) const
    {
        y += scrollOffset*static_cast<float>(width())/200.f;
        for (size_t i = 0; i < squareRects.size(); ++i) {
            if (squareRects[i].contains(0.f, y)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void TextureListDrawer::mousePressEvent(QMouseEvent *event)
    {
        QQuickItem::mousePressEvent(event);

        auto index = itemAtPosition(static_cast<float>(event->position().y()));
        if (index >= 0 && index < static_cast<int>(images.size())) {
            emit selectedImage(images[static_cast<size_t>(index)]);
        }
    }

    void TextureListDrawer::wheelEvent(QWheelEvent *event)
    {
        const auto maxScroll = static_cast<float>(implicitHeight() - height());
        if (maxScroll <= 0.f) {
            scrollOffset = 0;
            return;
        }
        const auto delta = event->angleDelta().y() / 120.f;
        const auto step = 20.f;

        scrollOffset -= delta * step;
        scrollOffset = qBound(0.f, scrollOffset, maxScroll/static_cast<float>(width())*200.f);

        update();
        event->accept();
    }

    void TextureListDrawer::draw()
    {
        auto device = tgfxWindow->getDevice();
        if (device == nullptr) {
            return;
        }
        auto context = device->lockContext();
        if (context == nullptr) {
            return;
        }
        auto surface = tgfxWindow->getSurface(context);
        if (surface == nullptr) {
            device->unlock();
            return;
        }
        auto canvas = surface->getCanvas();
        canvas->clear();

        canvas->setMatrix(tgfx::Matrix::MakeScale(appHost->density(), appHost->density()));
        canvas->translate(0, -static_cast<float>(scrollOffset*width())/200.f);
        for (size_t i = 0; i < squareRects.size(); ++i) {
            drawRect(canvas,squareRects[i], 0xFF535353);
            if (!images[i]) {
                return;
            }
            auto imageRect = calcInerRect(squareRects[i],images[i]->width() / static_cast<float>(images[i]->height()));
            canvas->drawImageRect(images[i],imageRect,tgfx::SamplingOptions(tgfx::FilterMode::Linear, tgfx::MipmapMode::Linear));
        }

        context->flushAndSubmit();
        tgfxWindow->present(context);
        device->unlock();
    }

    void TextureListDrawer::updateImageData(std::initializer_list<std::string>& testImageSources) {

        images.clear();
        for (auto&imageSource: testImageSources) {
            auto Image = tgfx::Image::MakeFromFile(imageSource);
            Image = Image->makeMipmapped(true);
            images.push_back(Image);
        }

        layoutDirty = true;
        update();
        emit selectedImage(nullptr);
    }

    void TextureListDrawer::setImageLabel(const QString &label) {
        std::string rootPath = QCoreApplication::applicationDirPath().toStdString();
        while (!rootPath.empty() && !std::filesystem::exists(rootPath + "/resources")) {
            rootPath = rootPath.substr(0, rootPath.find_last_of('/'));
        }
        if(label=="Input") {
            auto testImageSources = {
                rootPath + "/resources/assets/bridge.jpg",
                rootPath + "/resources/apitest/rotation.jpg",
                rootPath + "/resources/apitest/rgbaaa.png"};

            updateImageData(testImageSources);
        }
        else if (label=="Output") {
            auto testImageSources = {
                rootPath + "/resources/assets/glyph1.png",
                rootPath + "/resources/assets/glyph2.png",
                rootPath + "/resources/assets/glyph3.png",};

            updateImageData(testImageSources);
        }

    }

    QSGNode *TextureListDrawer::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
    {
        updateLayout();
        if (!tgfxWindow) {
            tgfxWindow = tgfx::QGLWindow::MakeFrom(this, true);
        }
        auto pixelRatio = window() ? static_cast<float>(window()->devicePixelRatio()) : 1.f;
        auto screenWidth = static_cast<int>(ceil(width() * pixelRatio));
        auto screenHeight = static_cast<int>(ceil(height() * pixelRatio));
        auto sizeChanged = appHost->updateScreen(screenWidth, screenHeight, pixelRatio);
        if (sizeChanged) {
            tgfxWindow->invalidSize();
        }

        draw();

        auto node = static_cast<QSGImageNode*>(oldNode);
        auto texture = tgfxWindow->getQSGTexture();
        if (texture) {
            if (!node) {
                node = window()->createImageNode();
                node->setFiltering(QSGTexture::Linear);
            }
            node->setTexture(texture);
            node->setRect(boundingRect());
            node->markDirty(QSGNode::DirtyMaterial | QSGNode::DirtyGeometry);
        }
        return node;
    }
}

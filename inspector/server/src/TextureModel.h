#pragma once
#include <QAbstractListModel>
#include "TextureDrawer.h"
#include "TextureListDrawer.h"

#include "ViewData.h"
#include "Worker.h"


namespace inspector {
    struct TextureModel  {
        std::unique_ptr<TextureDrawer> textureDrawer;
        std::unique_ptr<TextureListDrawer> inputTextureListDrawer;
        std::unique_ptr<TextureListDrawer> outputTextureListDrawer;
    };
}

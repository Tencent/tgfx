const path = require("path");
const CompressionPlugin = require("compression-webpack-plugin");
const CopyWebpackPlugin = require("copy-webpack-plugin");
const HtmlWebpackPlugin = require("html-webpack-plugin");
const WebpackMode = require("webpack-mode");

module.exports = {
    mode: `${WebpackMode}`,
    entry: "./emception.js",
    output: {
        filename: "[name].js",
        path: path.resolve(__dirname, "./dist"),
        clean: true,
        library: {
            type: "umd",
            name: "Emception",
        },
    },
    resolve: {
        alias: {
            emception: "../build/emception",
        },
        fallback: {
            "llvm-box.wasm": false,
            "binaryen-box.wasm": false,
            "python.wasm": false,
            "quicknode.wasm": false,
            "path": false,
            "node-fetch": false,
            "vm": false
        },
    },
    plugins: [
        new HtmlWebpackPlugin({
            title: "Emception",
        }),
        new CopyWebpackPlugin({
            patterns: [{
                from: "../build/emception/brotli/brotli.wasm",
                to: "brotli/brotli.wasm"
            }, {
                from: "../build/emception/wasm-package/wasm-package.wasm",
                to: "wasm-package/wasm-package.wasm"
            }],
        }),
        new CompressionPlugin({
            exclude: /\.br$/,
        }),
    ],
    module: {
        rules: [{
            test: /\.wasm$/,
            type: "asset/resource",
            // generator: {
            //     filename: "[name].[hash][ext]",
            // },
        }, {
            test: /\.(pack|br|a)$/,
            type: "asset/resource",
            // generator: {
            //     filename: "[name].[hash][ext]",
            // },
        }]
    },
    devServer: {
        allowedHosts: "auto",
        port: "auto",
        server: "https",
        headers: {
            "Cross-Origin-Embedder-Policy": "require-corp",
            "Cross-Origin-Opener-Policy": "same-origin",
        }
    },
};
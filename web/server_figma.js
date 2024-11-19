const express = require('express');
const app = express();
const path = require('path');
const fs = require('fs');

// Enable SharedArrayBuffer
app.use((req, res, next) => {
  res.set('Cross-Origin-Opener-Policy', 'same-origin');
  res.set('Cross-Origin-Embedder-Policy', 'require-corp');
  next();
});

app.use('/figma', express.static(path.join(__dirname, 'figma')));
app.use('/', express.static(path.join(__dirname, '../')));

app.get('/', (req, res) => {
  res.send('Hello, tgfx!');
});

const port = 8081;
const args = process.argv.slice(2);
app.listen(port, () => {
  console.log(`Server is running on port ${port}`);
  console.log(`当前工作目录: ${process.cwd()}`);
  
  fs.readdir(process.cwd(), (err, files) => {
    if (err) {
      return console.log('无法读取当前目录内容:', err);
    }
    console.log('当前目录内容:', files);
  });
  var url = `http://localhost:${port}/figma/index.html`;
  var start = (process.platform === 'darwin' ? 'open' : 'start');
  require('child_process').exec(`${start} ${url}`);
});
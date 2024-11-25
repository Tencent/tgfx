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

app.use('', express.static(path.join('../')));
app.use('', express.static(path.join('figma/')));

app.get('/', (req, res) => {
  res.send('Hello, tgfx!');
});

const port = 8091;
app.listen(port, () => {
  console.log(`Server is running on port ${port}`);
  console.log(`当前工作目录: ${process.cwd()}`);
  
  fs.readdir(process.cwd(), (err, files) => {
    if (err) {
      return console.log('无法读取当前目录内容:', err);
    }
    console.log('当前目录内容:', files);
  });
  var url = `http://localhost:${port}/index-mt.html`;
  var start = (process.platform === 'darwin' ? 'open' : 'start');
  require('child_process').exec(`${start} ${url}`);
});
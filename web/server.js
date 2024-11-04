const express = require('express');
const app = express();
const path = require('path');

// Enable SharedArrayBuffer
app.use((req, res, next) => {
  res.set('Cross-Origin-Opener-Policy', 'same-origin');
  res.set('Cross-Origin-Embedder-Policy', 'require-corp');
  next();
});

app.use('', express.static(path.join('../')));
app.use('', express.static(path.join('demo/')));

app.get('/', (req, res) => {
  res.send('Hello, tgfx!');
});

const port = 8081;
const args = process.argv.slice(2);
var fileName = args.includes('wasm-mt') ? 'index-mt': 'index';
app.listen(port, () => {
  console.log(`Server is running on port ${port}`);

  var url = `http://localhost:${port}/${fileName}.html`;
  if (args.includes('layer')) {
    url += '?type=layer';
  }
  var start = (process.platform == 'darwin'? 'open': 'start');
  require('child_process').exec(start + ' ' + url);
});
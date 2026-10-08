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
app.use('/resources', express.static(path.join('../resources')));

app.get('/', (req, res) => {
  res.send('Hello, tgfx!');
});

const port = 8081;
const args = process.argv.slice(2);
var fileName;
if(args.includes('worker-webgpu-st')){
  fileName = 'index-worker-webgpu-st';
}else if(args.includes('worker-webgpu-mt')){
  fileName = 'index-worker-webgpu-mt';
}else if(args.includes('worker-st')){
  fileName = 'index-worker-st';
}else if(args.includes('worker-mt')){
  fileName = 'index-worker-mt';
}else if(args.includes('webgpu')){
  fileName = args.includes('wasm-mt') ? 'index-webgpu':'index-webgpu-st';
}else{
  fileName = args.includes('wasm-mt') ? 'index':'index-st';
}
app.listen(port, () => {
  var url = `http://localhost:${port}/${fileName}.html`;
  var start = (process.platform == 'darwin'? 'open': 'start');
  require('child_process').exec(start + ' ' + url);
});

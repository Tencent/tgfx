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

app.get('/', (req, res) => {
  res.send('Hello, WebAssembly!');
});

const port = 8081;
app.listen(port, () => {
  console.log(`Server is running on port ${port}`);
});
const express = require('express');
const app = express();
const path = require('path');
const {exec} = require("child_process");

// Enable SharedArrayBuffer
app.use((req, res, next) => {
  res.set('Cross-Origin-Opener-Policy', 'same-origin');
  res.set('Cross-Origin-Embedder-Policy', 'require-corp');
  next();
});

app.use('', express.static(path.join('../')));

app.get('/', (req, res) => {
  res.send('Hello, tgfx!');
});

const port = 8081;
app.listen(port, () => {
  console.log(`Server is running on port ${port}`);
  
  const url = `http://localhost:${port}/web/demo/index.html`;
  const command = process.platform === 'win32' ? `start ${url}` : `open ${url}`;
  exec(command, (error, stdout, stderr) => {
    if (error) {
      return;
    }
  });
});
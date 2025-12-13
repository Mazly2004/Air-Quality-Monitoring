/** @type {import('tailwindcss').Config} */
module.exports = {
  content: [
    './src/**/*.{html,js,svelte,ts}',
  ],
  theme: {
    extend: {
      colors: {
        bg: '#0b1020',
        card: '#111936',
        outline: '#223',
        accent: '#60a5fa'
      }
    }
  },
  plugins: []
};

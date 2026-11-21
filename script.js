document.querySelectorAll('.download-button, .icon-button').forEach((link) => {
  link.addEventListener('click', () => {
    link.classList.add('clicked');
  });
});

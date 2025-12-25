// offscreen videos normally keep playing and this hurts my cpu,
// so we pause videos if they are offscreen.
var observer = new IntersectionObserver(
    (entries, observer) => entries.forEach(entry => {
        if (entry.isIntersecting) {
            entry.target.play();
        } else {
            entry.target.pause();
        }
    })
);

const videos = document.querySelectorAll('video');
videos.forEach(video => {
    // video are marked autoplay. This is so people without JS can watch the videos.
    // If they have JS, then we can pause them and then start them when they enter the viewport.
    video.pause(); 
    observer.observe(video);
});

function result=processFrame(adc,cfg)
%PROCESSFRAME Common pipeline for decoded synthetic or recorded ADC counts.
started=tic;
result.range=radar.processRange(adc,cfg);
result.doppler=radar.processDoppler(result.range.cube,cfg);
result.ddma=radar.decodeDdma(result.doppler.cube,cfg);
result.detection=radar.detectAndEstimate(result.ddma,result.range.rangeAxisM,cfg);
result.pointCloud=radar.makePointCloud(result.detection.detections,cfg);
result.elapsedS=toc(started);
end
